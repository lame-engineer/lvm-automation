
# Phase A — Disk & Filesystem Scanner

This repository contains a small **system inspection tool** written in **C** (for speed and minimal dependencies) and a **Python wrapper** (for ease of integration and automation).  
It scans mounted devices and outputs detailed disk information in **JSON** format.

---

## Features

- **Phase A** capabilities:
  - Detect total disk size
  - Report starting and ending block numbers
  - Detect swap size
  - Identify filesystem type
  - Check if LVM is present
  - Detect LVM encryption status
  - Detect full-disk encryption status
  - List mounted devices and mount points

- **C binary** produces clean, machine-parseable JSON.
- **Python wrapper** automatically:
  - Compiles the C program (via `gcc`) if needed.
  - Runs the compiled binary.
  - Parses and returns JSON output for further processing.

---

## Requirements

- **For C program**:
  - GCC (GNU Compiler Collection)
  - POSIX-compatible OS (Linux, BSD, etc.)

- **For Python wrapper**:
  - Python 3.x
  - `gcc` available in `$PATH`

---

## Usage

### Run with Python (Recommended)
```bash
python3 run_phase_a.py
````

* The Python script will:

  1. Compile `phase_a.c` into a binary named `phase_a`.
  2. Run the binary.
  3. Parse the JSON output and print it.

### Run C Program Directly

```bash
gcc -o phase_a phase_a.c
sudo ./phase_a
```

* Direct execution is faster but requires manual compilation.

---

## Output Format

Example JSON output:

```json
{
  "mounts": [
    {
      "device": "/dev/sda1",
      "mount_point": "/",
      "filesystem": "ext4",
      "total_size_bytes": 256000000000,
      "start_block": 2048,
      "end_block": 500118191,
      "lvm": false,
      "lvm_encrypted": false,
      "disk_encrypted": false
    },
    {
      "device": "swap",
      "mount_point": "[SWAP]",
      "swap_size_bytes": 8192000000
    }
  ]
}
```

---

## Notes

* Running as **root** (`sudo`) is recommended to ensure all block devices can be read.
* Works best on Linux systems with `/proc` and `/sys` filesystems available.
* For now, only **Phase A** scanning is implemented — future phases will expand functionality.


# yet to implement phase_B

---

## **Phase B — Planned Automation**

### **Features to Implement**

#### **1. Deep LVM Analysis**

* Detect and list all Logical Volume Manager (LVM) volumes.
* Identify if any LVM volumes are encrypted.
* Extract detailed LVM metadata (volume group, logical volumes, sizes, UUIDs).
* Map physical volumes to logical volumes.

#### **2. Full Disk Encryption Analysis**

* Detect if a disk is fully encrypted using **LUKS** or **dm-crypt**.
* Extract encryption metadata such as cipher, key size, and UUID.
* Verify if encrypted volumes are currently unlocked.

#### **3. Filesystem Health Checks**

* Perform filesystem integrity checks.
* Retrieve free/used space per mount point.
* Detect filesystem type, version, and any detected errors.

#### **4. Automated Reporting**

* Export results in **JSON**, **CSV**, and **HTML** formats.
* Include both raw scan data and human-readable summaries.

---
# yet to implement phase_C

#### **Actions** 

* Securely wipe a disk or partition.
* Resize partitions or logical volumes.
* Trigger an automated backup before risky operations.

---
