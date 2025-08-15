import subprocess
import sys
import os
import json

C_FILE = "phase_a.c"
BINARY = "phase_a"

def compile_c_program():
    """Compile the C file into a binary if needed."""
    if not os.path.exists(C_FILE):
        print(f"[ERROR] {C_FILE} not found in current directory.")
        sys.exit(1)

    needs_compile = (
        not os.path.exists(BINARY) or
        os.path.getmtime(C_FILE) > os.path.getmtime(BINARY)
    )

    if needs_compile:
        print(f"[INFO] Compiling {C_FILE}...")
        try:
            subprocess.run(
                ["gcc", C_FILE, "-o", BINARY],
                check=True
            )
            print("[INFO] Compilation successful.")
        except subprocess.CalledProcessError as e:
            print(f"[ERROR] Compilation failed with code {e.returncode}")
            sys.exit(1)
    else:
        print("[INFO] Using existing compiled binary.")

def run_binary():
    """Run the compiled binary and parse its JSON output."""
    if not os.path.exists(BINARY):
        print(f"[ERROR] {BINARY} not found. Compilation may have failed.")
        sys.exit(1)

    try:
        result = subprocess.run(
            [f"./{BINARY}"],
            capture_output=True,
            text=True,
            check=True
        )
        output = result.stdout.strip()

        try:
            data = json.loads(output)
            return data
        except json.JSONDecodeError as je:
            print("[ERROR] Failed to parse JSON from C output.")
            print("Raw output was:\n", output)
            sys.exit(1)

    except subprocess.CalledProcessError as e:
        print(f"[ERROR] {BINARY} exited with code {e.returncode}")
        if e.stderr:
            print(e.stderr.strip())
        sys.exit(1)

if __name__ == "__main__":
    compile_c_program()
    system_info = run_binary()

    print("=== Parsed Phase A Data ===")
    print(json.dumps(system_info, indent=4))

    # Example access:
    print("\nSwap current:", system_info["swap_current_bytes"], "bytes")
    print("Swap recommended:", system_info["swap_recommended_bytes"], "bytes")
    print("LVM present:", system_info["lvm_present"])
    print("Encryption present:", system_info["encryption_present"])
