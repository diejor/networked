import subprocess
import json
import sys
import os

def run_command(args):
    try:
        result = subprocess.run(args, capture_output=True, text=True, check=True)
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None

def get_info():
    # Try Jujutsu (jj) first
    jj_template = (
        '"{ \\"commit\\": \\"" ++ commit_id ++ "\\", '
        '\\"change\\": \\"" ++ change_id.short() ++ "\\", '
        '\\"summary\\": \\"" ++ (if(description, description.first_line(), "none")) ++ "\\" }"'
    )
    jj_output = run_command(["jj", "log", "-r", "@", "-n", "1", "--no-graph", "-T", jj_template])
    
    if jj_output:
        try:
            return json.loads(jj_output)
        except json.JSONDecodeError:
            pass

    # Fallback to Git
    git_hash = run_command(["git", "rev-parse", "HEAD"])
    if git_hash:
        git_summary = run_command(["git", "log", "-1", "--pretty=%s"])
        return {
            "commit": git_hash,
            "change": "n/a (git)",
            "summary": git_summary or "none"
        }

    # Last resort
    return {"error": "No version control system detected"}

def main():
    info = get_info()
    with open("version.json", "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
    print(f"Successfully updated version.json using {'jj' if info.get('change') != 'n/a (git)' else 'git'}")

if __name__ == "__main__":
    main()