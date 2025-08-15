// phase_a.c - Argument-free Linux storage scanner (Phase A)
// Build: gcc -O2 -Wall -Wextra -o phase_a phase_a.c -lblkid

#define _GNU_SOURCE
#include <blkid/blkid.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR_BYTES_DEFAULT 512

// ---------- small helpers ----------
static int read_ull(const char *path, unsigned long long *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    unsigned long long v = 0;
    int rc = fscanf(f, "%llu", &v);
    fclose(f);
    if (rc != 1) return -1;
    *out = v;
    return 0;
}

typedef struct MountInfo {
    char *dev;        // device (may be /dev/mapper/..., tmpfs, sysfs, etc.)
    char *realdev;    // resolved realpath if dev is a symlink; else copy of dev
    char *mnt;        // mountpoint
    char *fstype;     // fstype
    struct MountInfo *next;
} MountInfo;

static MountInfo* load_mounts(void) {
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return NULL;
    MountInfo *head = NULL;
    char dev[PATH_MAX], mnt[PATH_MAX], fstype[128];
    // device mount fstype options dump pass
    while (fscanf(f, "%1023s %1023s %127s %*s %*d %*d\n", dev, mnt, fstype) == 3) {
        MountInfo *mi = calloc(1, sizeof(*mi));
        if (!mi) break;
        mi->dev = strdup(dev);
        mi->mnt = strdup(mnt);
        mi->fstype = strdup(fstype);
        // resolve real path for /dev/* entries – helps match /sys/class/block names
        if (strncmp(dev, "/dev/", 5) == 0) {
            char buf[PATH_MAX];
            if (realpath(dev, buf)) mi->realdev = strdup(buf);
            else mi->realdev = strdup(dev);
        } else {
            mi->realdev = strdup(dev);
        }
        mi->next = head;
        head = mi;
    }
    fclose(f);
    return head;
}

static void free_mounts(MountInfo *head) {
    while (head) {
        MountInfo *n = head->next;
        free(head->dev);
        free(head->realdev);
        free(head->mnt);
        free(head->fstype);
        free(head);
        head = n;
    }
}

// find by exact device path match
static const MountInfo* find_mount_by_dev(const MountInfo *head, const char *devpath) {
    for (const MountInfo *p = head; p; p = p->next) {
        if (p->dev && strcmp(p->dev, devpath) == 0) return p;
        if (p->realdev && strcmp(p->realdev, devpath) == 0) return p;
    }
    return NULL;
}

static unsigned long long read_swap_total_bytes(void) {
    FILE *f = fopen("/proc/swaps", "r");
    if (!f) return 0;
    char line[1024];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; } // header
    unsigned long long sum_kb = 0;
    char dev[PATH_MAX], type[64];
    unsigned long long size_kb = 0, used_kb = 0;
    int prio = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%1023s %63s %llu %llu %d", dev, type, &size_kb, &used_kb, &prio) == 5) {
            sum_kb += size_kb;
        }
    }
    fclose(f);
    return sum_kb * 1024ULL;
}

static unsigned long long read_ram_total_bytes(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char key[64]; unsigned long long kb = 0;
    while (fscanf(f, "%63s %llu kB\n", key, &kb) == 2) {
        if (strcmp(key, "MemTotal:") == 0) { fclose(f); return kb * 1024ULL; }
    }
    fclose(f);
    return 0;
}

static char* devpath_from_name(const char *name) {
    char *p = NULL;
    if (asprintf(&p, "/dev/%s", name) < 0) return NULL;
    return p;
}

// Probe TYPE (filesystem or signature). For LVM/LUKS libblkid returns TYPE=LVM2_member/crypto_LUKS.
static char* blkid_type(const char *devpath) {
    blkid_probe pr = blkid_new_probe_from_filename(devpath);
    if (!pr) return NULL;
    blkid_do_safeprobe(pr);
    const char *val = NULL; size_t len = 0;
    char *out = NULL;
    if (blkid_probe_lookup_value(pr, "TYPE", &val, &len) == 0 && val) {
        out = strndup(val, len);
    }
    blkid_free_probe(pr);
    return out;
}

// Escape minimal JSON
static void json_escape(const char *s, FILE *out) {
    for (; s && *s; s++) {
        if (*s == '\\' || *s == '\"') { fputc('\\', out); fputc(*s, out); }
        else if (*s == '\n') fputs("\\n", out);
        else fputc(*s, out);
    }
}

static int is_real_disk(const char *name) {
    // consider a "disk" if /sys/block/<name>/device exists
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/block/%s/device", name);
    struct stat st;
    return stat(path, &st) == 0;
}

static int is_partition_of(const char *disk, const char *entry) {
    // partition name starts with disk name and is longer
    size_t dl = strlen(disk);
    return (strncmp(entry, disk, dl) == 0) && (strlen(entry) > dl);
}

int main(void) {
    MountInfo *mounts = load_mounts();

    unsigned long long swap_bytes = read_swap_total_bytes();
    unsigned long long ram_bytes  = read_ram_total_bytes();
    unsigned long long rec_swap   = ram_bytes ? (ram_bytes * 3 / 2) : (16ULL << 30);
    const unsigned long long cap_16g = (16ULL << 30);
    if (rec_swap > cap_16g) rec_swap = cap_16g;

    int lvm_present = 0, encryption_present = 0;

    printf("{");
    printf("\"swap_current_bytes\":%llu,", swap_bytes);
    printf("\"swap_recommended_bytes\":%llu,", rec_swap);
    printf("\"disks\":[");

    DIR *sb = opendir("/sys/block");
    if (!sb) {
        perror("opendir /sys/block");
        free_mounts(mounts);
        return 1;
    }

    struct dirent *de;
    int first_disk = 1;
    while ((de = readdir(sb)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!is_real_disk(de->d_name)) continue;

        char *disk_dev = devpath_from_name(de->d_name);
        if (!disk_dev) continue;

        unsigned long long hw_sector = SECTOR_BYTES_DEFAULT;
        unsigned long long total_sectors = 0;
        char pth[PATH_MAX];

        snprintf(pth, sizeof(pth), "/sys/block/%s/queue/hw_sector_size", de->d_name);
        read_ull(pth, &hw_sector);
        snprintf(pth, sizeof(pth), "/sys/block/%s/size", de->d_name);
        read_ull(pth, &total_sectors);
        unsigned long long total_bytes = total_sectors * hw_sector;

        if (!first_disk) printf(",");
        first_disk = 0;
        printf("{");
        printf("\"name\":\""); json_escape(de->d_name, stdout); printf("\",");
        printf("\"path\":\""); json_escape(disk_dev, stdout); printf("\",");
        printf("\"sector_size\":%llu,", hw_sector);
        printf("\"size_sectors\":%llu,", total_sectors);
        printf("\"size_bytes\":%llu,", total_bytes);
        printf("\"partitions\":[");

        // Iterate /sys/class/block for partitions of this disk
        DIR *cb = opendir("/sys/class/block");
        int first_part = 1;
        if (cb) {
            struct dirent *ce;
            while ((ce = readdir(cb)) != NULL) {
                if (ce->d_name[0] == '.') continue;
                if (!is_partition_of(de->d_name, ce->d_name)) continue;

                unsigned long long pstart = 0, psize = 0;
                char sp[PATH_MAX], zp[PATH_MAX];
                snprintf(sp, sizeof(sp), "/sys/class/block/%s/start", ce->d_name);
                snprintf(zp, sizeof(zp), "/sys/class/block/%s/size",  ce->d_name);
                read_ull(sp, &pstart);
                read_ull(zp, &psize);
                unsigned long long pend = (psize ? (pstart + psize - 1) : pstart);

                char *part_dev = devpath_from_name(ce->d_name);
                if (!part_dev) continue;

                // mount info (exact match or resolved symlink)
                const MountInfo *mi = find_mount_by_dev(mounts, part_dev);

                // probe type (ext4/xfs/LVM2_member/crypto_LUKS)
                char *type = blkid_type(part_dev);
                int is_lvm_member = (type && strcmp(type, "LVM2_member") == 0);
                int is_luks       = (type && strcmp(type, "crypto_LUKS") == 0);
                if (is_lvm_member) lvm_present = 1;
                if (is_luks)       encryption_present = 1;

                if (!first_part) printf(",");
                first_part = 0;
                printf("{");
                printf("\"name\":\""); json_escape(ce->d_name, stdout); printf("\",");
                printf("\"path\":\""); json_escape(part_dev, stdout); printf("\",");
                printf("\"start_sector\":%llu,", pstart);
                printf("\"size_sectors\":%llu,", psize);
                printf("\"end_sector\":%llu,", pend);
                printf("\"mountpoint\":");
                if (mi && mi->mnt) { printf("\""); json_escape(mi->mnt, stdout); printf("\""); }
                else printf("null");
                printf(",");
                printf("\"fs_type\":");
                if (mi && mi->fstype) { printf("\""); json_escape(mi->fstype, stdout); printf("\""); }
                else if (type)        { printf("\""); json_escape(type, stdout);     printf("\""); }
                else printf("null");
                printf(",");
                printf("\"is_lvm_member\":%s,", is_lvm_member ? "true" : "false");
                printf("\"is_luks\":%s", is_luks ? "true" : "false");
                printf("}");

                free(part_dev);
                free(type);
            }
            closedir(cb);
        }

        printf("]}");
        free(disk_dev);
    }
    closedir(sb);
    printf("]");

    printf(",\"lvm_present\":%s", lvm_present ? "true" : "false");
    printf(",\"encryption_present\":%s", encryption_present ? "true" : "false");
    printf("}\n");

    free_mounts(mounts);
    return 0;
}
