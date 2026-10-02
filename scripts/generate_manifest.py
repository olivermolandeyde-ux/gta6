# Generates a manifest of all files for delta patching
import os
import hashlib
import json

def generate_manifest(root_dir):
    manifest = {
        "version": "1.0.0",
        "files": {}
    }

    for root, dirs, files in os.walk(root_dir):
        for file in files:
            filepath = os.path.join(root, file)
            relpath = os.path.relpath(filepath, root_dir)

            with open(filepath, 'rb') as f:
                content = f.read()
                checksum = hashlib.sha256(content).hexdigest()

            manifest["files"][relpath] = {
                "size": len(content),
                "sha256": checksum
            }

    return manifest

if __name__ == "__main__":
    import sys
    root_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    manifest = generate_manifest(root_dir)
    print(json.dumps(manifest, indent=2))
