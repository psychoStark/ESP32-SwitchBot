#!/usr/bin/env python3
"""
Interactive CLI setup tool to generate main/secrets.h for ESP32-SwitchBot.
Compatible with macOS, Linux, Windows, and Android (Termux).
Requires Python 3.6+ standard library only.
"""

import os
import sys
import re
import time
import shutil
import getpass
import warnings
import textwrap
from pathlib import Path

# Silence getpass fallback warning in non-interactive/piped environments
warnings.filterwarnings("ignore", category=getpass.GetPassWarning)

# Safe stdout reconfigure for UTF-8 box characters across Windows/Unix
try:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

# Enable ANSI colors on Windows 10+ consoles if available
if sys.platform == "win32":
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
    except Exception:
        pass

# Color codes (disabled if stdout is redirected or NO_COLOR environment variable is set)
USE_COLOR = sys.stdout.isatty() and "NO_COLOR" not in os.environ
CYAN = "\033[96m" if USE_COLOR else ""
GREEN = "\033[92m" if USE_COLOR else ""
YELLOW = "\033[93m" if USE_COLOR else ""
RED = "\033[91m" if USE_COLOR else ""
BOLD = "\033[1m" if USE_COLOR else ""
DIM = "\033[2m" if USE_COLOR else ""
RESET = "\033[0m" if USE_COLOR else ""

# Box & UI Glyphs (clean Unicode with ASCII fallback if needed)
try:
    # Test if box characters can encode
    "┌─┐└─┘│├┤┬┴┼▸✔↺⚠ℹ".encode(sys.stdout.encoding or "utf-8")
    BOX_TL = "┌"
    BOX_TR = "┐"
    BOX_BL = "└"
    BOX_BR = "┘"
    BOX_H  = "─"
    BOX_V  = "│"
    BOX_LT = "├"
    BOX_RT = "┤"
    BOX_TT = "┬"
    BOX_BT = "┴"
    BOX_C  = "┼"
    GLYPH_PROMPT = "▸"
    GLYPH_CHECK  = "✔"
    GLYPH_RETRY  = "↺"
    GLYPH_WARN   = "⚠"
    GLYPH_INFO   = "ℹ"
except Exception:
    BOX_TL = "+"
    BOX_TR = "+"
    BOX_BL = "+"
    BOX_BR = "+"
    BOX_H  = "-"
    BOX_V  = "|"
    BOX_LT = "+"
    BOX_RT = "+"
    BOX_TT = "+"
    BOX_BT = "+"
    BOX_C  = "+"
    GLYPH_PROMPT = ">"
    GLYPH_CHECK  = "[OK]"
    GLYPH_RETRY  = "[RETRY]"
    GLYPH_WARN   = "[!]"
    GLYPH_INFO   = "[*]"

BOX_WIDTH = 74
INNER_WIDTH = BOX_WIDTH - 2  # 72 columns of text between borders

REPO_ROOT = Path(__file__).resolve().parent
TARGET_FILE = REPO_ROOT / "main" / "secrets.h"

# CLI Argument parsing
if "-h" in sys.argv or "--help" in sys.argv:
    print(f"Usage: {sys.argv[0]} [--target <path/to/secrets.h>]")
    print("Interactive setup tool to generate main/secrets.h")
    sys.exit(0)

if "--target" in sys.argv:
    try:
        idx = sys.argv.index("--target")
        TARGET_FILE = Path(sys.argv[idx + 1]).resolve()
    except IndexError:
        print(f"{RED}Error: --target requires a path argument{RESET}")
        sys.exit(1)

CLEAR_KEYWORDS = ("-", "clear", "none", "delete", "off", "remove")

FIELDS = [
    {
        "key": "WIFI_SSID",
        "category": "Wi-Fi Configuration",
        "title": "Wi-Fi Network SSID",
        "desc": "Name of your Wi-Fi network.",
        "default": "",
        "required": True,
        "is_secret": False,
        "visible_prefix": "",
    },
    {
        "key": "WIFI_PASSWORD",
        "category": "Wi-Fi Configuration",
        "title": "Wi-Fi Password",
        "desc": "WPA/WPA2 security passphrase for your Wi-Fi network.",
        "default": "",
        "required": False,
        "is_secret": True,
        "visible_prefix": "",
    },
    {
        "key": "TAILSCALE_KEY",
        "category": "Tailscale Network",
        "title": "Tailscale Auth Key",
        "desc": "Pre-authenticated key (tskey-auth-...) from Admin Console.",
        "default": "",
        "required": False,
        "is_secret": True,
        "visible_prefix": "tskey-auth-",
    },
    {
        "key": "TAILSCALE_HOST",
        "category": "Tailscale Network",
        "title": "Tailscale Hostname",
        "desc": "Device hostname on your Tailnet.",
        "default": "esp32",
        "required": False,
        "is_secret": False,
        "visible_prefix": "",
    },
    {
        "key": "TAILSCALE_API_KEY",
        "category": "Subnet Watchdog (Optional)",
        "title": "Tailscale API Access Token",
        "desc": "Read-only API access token for subnet failover monitoring.",
        "default": "",
        "required": False,
        "is_secret": True,
        "visible_prefix": "tskey-api-",
    },
    {
        "key": "TAILSCALE_SUBNET_DEVICE_ID",
        "category": "Subnet Watchdog (Optional)",
        "title": "Primary Subnet Device ID",
        "desc": "Primary subnet router hostname (e.g. moto-g32) or machine ID.",
        "default": "",
        "required": False,
        "is_secret": False,
        "visible_prefix": "",
    },
    {
        "key": "OTA_KEY",
        "category": "Device Security",
        "title": "OTA Security Password",
        "desc": "PIN or passphrase required to authorize /ota/enable updates.",
        "default": "",
        "required": False,
        "is_secret": True,
        "visible_prefix": "",
    },
]


def c_escape(val: str) -> str:
    """Escape backslashes and quotes for C/C++ string literals."""
    return val.replace("\\", "\\\\").replace('"', '\\"')


def mask_value(val: str, key: str) -> str:
    """Mask value according to specific formatting rules."""
    if not val:
        if key in ("WIFI_PASSWORD", "OTA_KEY"):
            return f"{DIM}(no password){RESET}"
        return f"{DIM}(disabled){RESET}"

    if key == "WIFI_PASSWORD":
        # First and last 3 chars visible, followed by (X chars)
        length = len(val)
        if length > 6:
            ast_count = min(8, length - 6)
            masked = val[:3] + ("*" * ast_count) + val[-3:]
        elif length > 2:
            ast_count = min(6, length - 2)
            masked = val[:1] + ("*" * ast_count) + val[-1:]
        else:
            masked = "*" * length
        return f"{masked} {DIM}({length} chars){RESET}"

    elif key == "OTA_KEY":
        # First and last 1 char visible, followed by (X chars)
        length = len(val)
        if length > 2:
            ast_count = min(8, length - 2)
            masked = val[:1] + ("*" * ast_count) + val[-1:]
        elif length == 2:
            masked = val[:1] + "*"
        else:
            masked = "*" * length
        return f"{masked} {DIM}({length} chars){RESET}"

    elif key == "TAILSCALE_KEY":
        # tskey-auth- visible, asterisks, last 4 chars visible, NO char count
        prefix = "tskey-auth-"
        if val.startswith(prefix):
            secret = val[len(prefix):]
            if len(secret) > 4:
                ast_count = min(9, len(secret) - 4)
                masked = prefix + ("*" * ast_count) + secret[-4:]
            else:
                masked = prefix + ("*" * len(secret))
        else:
            if len(val) > 4:
                ast_count = min(9, len(val) - 4)
                masked = ("*" * ast_count) + val[-4:]
            else:
                masked = "*" * len(val)
        return masked

    elif key == "TAILSCALE_API_KEY":
        # tskey-api- visible, asterisks, last 4 chars visible, NO char count
        prefix = "tskey-api-"
        if val.startswith(prefix):
            secret = val[len(prefix):]
            if len(secret) > 4:
                ast_count = min(8, len(secret) - 4)
                masked = prefix + ("*" * ast_count) + secret[-4:]
            else:
                masked = prefix + ("*" * len(secret))
        else:
            if len(val) > 4:
                ast_count = min(8, len(val) - 4)
                masked = ("*" * ast_count) + val[-4:]
            else:
                masked = "*" * len(val)
        return masked

    elif len(val) <= 6:
        return "*" * len(val)
    return val[:3] + "*" * min(8, len(val) - 6) + val[-3:]


def read_single_key(allowed_keys: tuple = (), default: str = "") -> str:
    """Read a single keypress immediately without waiting for Enter."""
    if not sys.stdin.isatty():
        line = sys.stdin.readline().strip().lower()
        res = line[:1] if line else default
        return res

    is_windows = sys.platform == "win32"
    if is_windows:
        import msvcrt
        while True:
            ch = msvcrt.getwch()
            if ch in ("\r", "\n"):
                res = default
                break
            if ch == "\x03":
                raise KeyboardInterrupt()
            res = ch.lower()
            if not allowed_keys or res in allowed_keys:
                break
    else:
        import termios
        import tty

        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        tty.setcbreak(fd)
        try:
            while True:
                ch = sys.stdin.read(1)
                if ch in ("\r", "\n"):
                    res = default
                    break
                if ch == "\x03":
                    raise KeyboardInterrupt()
                if ch == "\x04":
                    raise EOFError()
                res = ch.lower()
                if not allowed_keys or res in allowed_keys:
                    break
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

    # Print the pressed key and newline
    sys.stdout.write(f"{res}\n")
    sys.stdout.flush()
    return res


def read_masked_input(prompt: str, visible_prefix: str = "", timeout: float = 1.0) -> str:
    """
    Read input while masking characters as '*', with the last typed character
    flashing for `timeout` seconds (or until the next character is typed).
    If `visible_prefix` is given (e.g. 'tskey-auth-'), that prefix is NOT pre-filled,
    but becomes visible as it is typed or pasted.
    """
    if not sys.stdin.isatty():
        raw = input(prompt).strip()
        if raw.lower() in CLEAR_KEYWORDS:
            return raw
        if visible_prefix and raw:
            if raw.startswith(visible_prefix):
                return raw
            return f"{visible_prefix}{raw}"
        return raw

    suffix_chars = []
    last_char_visible = False

    def render():
        nonlocal last_char_visible
        current_str = "".join(suffix_chars)
        if not current_str:
            content = ""
        elif current_str.strip().lower() in CLEAR_KEYWORDS:
            content = current_str
        elif visible_prefix:
            if current_str.startswith(visible_prefix):
                prefix_len = len(visible_prefix)
                secret = current_str[prefix_len:]
                if secret:
                    if last_char_visible:
                        masked = ("*" * (len(secret) - 1)) + secret[-1]
                    else:
                        masked = "*" * len(secret)
                else:
                    masked = ""
                content = visible_prefix + masked
            elif visible_prefix.startswith(current_str):
                # User is actively typing prefix characters
                content = current_str
            else:
                if last_char_visible:
                    content = ("*" * (len(current_str) - 1)) + current_str[-1]
                else:
                    content = "*" * len(current_str)
        else:
            if last_char_visible:
                content = ("*" * (len(current_str) - 1)) + current_str[-1]
            else:
                content = "*" * len(current_str)

        sys.stdout.write(f"\r\033[K{prompt}{content}")
        sys.stdout.flush()

    render()

    is_windows = sys.platform == "win32"
    if is_windows:
        import msvcrt
    else:
        import termios
        import tty
        import select

        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        tty.setcbreak(fd)

    try:
        while True:
            if is_windows:
                if last_char_visible:
                    start_t = time.time()
                    has_key = False
                    while time.time() - start_t < timeout:
                        if msvcrt.kbhit():
                            has_key = True
                            break
                        time.sleep(0.02)
                    if not has_key:
                        last_char_visible = False
                        render()
                ch = msvcrt.getwch()
            else:
                if last_char_visible:
                    r, _, _ = select.select([sys.stdin], [], [], timeout)
                    if not r:
                        last_char_visible = False
                        render()

                chunk = os.read(fd, 1024).decode("utf-8", errors="replace")
                if not chunk:
                    raise EOFError()
                ch = chunk

            # Handle multi-character paste
            if not is_windows and len(ch) > 1:
                pasted = ch.replace("\r\n", "\n").replace("\r", "\n")
                ended = "\n" in pasted
                if ended:
                    pasted, _, _ = pasted.partition("\n")

                for c in pasted:
                    if c >= " ":
                        suffix_chars.append(c)

                if ended:
                    last_char_visible = False
                    render()
                    sys.stdout.write("\n")
                    sys.stdout.flush()
                    break

                last_char_visible = True
                render()
                continue

            # Handle single keypress
            c = ch
            if c in ("\r", "\n"):
                last_char_visible = False
                render()
                sys.stdout.write("\n")
                sys.stdout.flush()
                break
            elif c in ("\x08", "\x7f"):  # Backspace
                if suffix_chars:
                    suffix_chars.pop()
                last_char_visible = False
                render()
            elif c == "\x03":  # Ctrl+C
                raise KeyboardInterrupt()
            elif c == "\x04":  # Ctrl+D
                raise EOFError()
            elif c >= " ":  # Printable character
                suffix_chars.append(c)
                last_char_visible = True
                render()

    finally:
        if not is_windows:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

    typed = "".join(suffix_chars)
    if typed.strip().lower() in CLEAR_KEYWORDS:
        return typed.strip()
    if visible_prefix and typed:
        if typed.startswith(visible_prefix):
            return typed
        return f"{visible_prefix}{typed}"
    return typed


def prompt_field(field: dict, step_num: int, total_steps: int, current_val: str = None) -> str:
    """
    Prompt user for a value with a clean card interface,
    then ask to confirm [Y/n]. If 'n', immediately re-enter.
    """
    key = field["key"]
    title = field["title"]
    category = field["category"]
    desc = field["desc"]
    default = field["default"]
    required = field["required"]
    prefix = field.get("visible_prefix", "")

    while True:
        # Card header
        header_text = f" [Step {step_num}/{total_steps}] {category} "
        pad_len = BOX_WIDTH - 2 - 1 - len(header_text)
        print(f"\n{CYAN}{BOX_TL}{BOX_H}{BOLD}{header_text}{RESET}{CYAN}{BOX_H * pad_len}{BOX_TR}{RESET}")

        def print_card_line(label: str, text: str, is_bold_label: bool = False, is_dim_text: bool = False):
            prefix_str = f"  {label:<12}: "
            indent = " " * len(prefix_str)
            avail = INNER_WIDTH - len(prefix_str)
            wrapped = textwrap.wrap(text, width=avail, break_long_words=False, break_on_hyphens=False) or [""]
            for i, line in enumerate(wrapped):
                if i == 0:
                    lbl = f"{BOLD}{prefix_str}{RESET}" if is_bold_label else f"{DIM}{prefix_str}{RESET}"
                    txt = f"{DIM}{line}{RESET}" if is_dim_text else line
                    raw_len = len(prefix_str) + len(line)
                    pad = INNER_WIDTH - raw_len
                    print(f"{CYAN}{BOX_V}{RESET}{lbl}{txt}{' ' * pad}{CYAN}{BOX_V}{RESET}")
                else:
                    txt = f"{DIM}{line}{RESET}" if is_dim_text else line
                    raw_len = len(indent) + len(line)
                    pad = INNER_WIDTH - raw_len
                    print(f"{CYAN}{BOX_V}{RESET}{indent}{txt}{' ' * pad}{CYAN}{BOX_V}{RESET}")

        print_card_line("Setting", f"{title} ({key})", is_bold_label=True)
        print_card_line("Description", desc, is_dim_text=True)

        # Instructions / existing values
        has_existing = current_val is not None and current_val != ""

        if has_existing:
            raw_curr = mask_value(current_val, key) if field["is_secret"] else current_val
            clean_curr = re.sub(r'\033\[[0-9;]*m', '', raw_curr)
            print_card_line("Existing", clean_curr, is_dim_text=True)

            if not required:
                print_card_line("Options", "[Enter] keep existing  |  Type '-' or 'clear' to remove", is_dim_text=True)
            elif default and current_val != default:
                print_card_line("Options", f"[Enter] keep existing  |  Type 'default' to reset to {default}", is_dim_text=True)
            else:
                print_card_line("Options", "Press [Enter] to keep existing", is_dim_text=True)
        else:
            if default:
                print_card_line("Options", f"Press [Enter] to use default ({default})", is_dim_text=True)
            elif not required:
                opt_label = "no password" if key in ("WIFI_PASSWORD", "OTA_KEY") else "disabled"
                print_card_line("Options", f"Press [Enter] to skip ({opt_label})", is_dim_text=True)
            else:
                print_card_line("Options", "Required (cannot be empty)", is_dim_text=True)

        print(f"{CYAN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")

        try:
            prompt_text = f"{BOLD}{CYAN}{GLYPH_PROMPT} Enter value: {RESET}"
            if field["is_secret"]:
                val = read_masked_input(prompt_text, visible_prefix=prefix, timeout=1.0)
            else:
                val = input(prompt_text).strip()
        except (KeyboardInterrupt, EOFError):
            print(f"\n\n{RED}{GLYPH_WARN} Setup cancelled by user.{RESET}\n")
            sys.exit(1)

        # Handle clear / reset commands
        if val.strip().lower() in CLEAR_KEYWORDS:
            if required:
                print(f"{RED}{GLYPH_WARN} This field is required and cannot be cleared. Please enter a value.{RESET}")
                continue
            if default and key == "TAILSCALE_HOST":
                val = default
            else:
                val = ""
        elif val.strip().lower() == "default" and default:
            val = default
        elif not val:
            # User pressed Enter
            if has_existing:
                val = current_val
            elif default:
                val = default
            elif required:
                print(f"{RED}{GLYPH_WARN} This field is required and cannot be empty. Please enter a value.{RESET}")
                continue
            else:
                val = ""

        # Display what was entered
        if val != "":
            if field["is_secret"]:
                display_val = mask_value(val, key)
            else:
                display_val = f'"{val}"'
        else:
            if key in ("WIFI_PASSWORD", "OTA_KEY"):
                display_val = f"{DIM}(no password){RESET}"
            else:
                display_val = f"{DIM}(disabled){RESET}"

        print(f"\n  {GREEN}{GLYPH_CHECK}{RESET} Value entered : {BOLD}{display_val}{RESET}")

        # Single key Y/n confirmation prompt (no Enter key required)
        while True:
            print(f"  {YELLOW}{GLYPH_PROMPT} Confirm this value? [Y/n]: {RESET}", end="", flush=True)
            try:
                choice = read_single_key(allowed_keys=("y", "n"), default="y")
            except (KeyboardInterrupt, EOFError):
                print(f"\n\n{RED}{GLYPH_WARN} Setup cancelled by user.{RESET}\n")
                sys.exit(1)

            if choice == "y":
                return val
            elif choice == "n":
                print(f"\n  {YELLOW}{GLYPH_RETRY} Retyping {title}...{RESET}")
                break


def generate_header_content(values: dict) -> str:
    """Generate the C++ header file content."""
    lines = [
        "#pragma once",
        "",
        "// Wi-Fi Credentials",
        f'#define WIFI_SSID        "{c_escape(values.get("WIFI_SSID", ""))}"',
        f'#define WIFI_PASSWORD    "{c_escape(values.get("WIFI_PASSWORD", ""))}"',
        "",
        "// Tailscale Configuration",
        f'#define TAILSCALE_KEY    "{c_escape(values.get("TAILSCALE_KEY", ""))}"',
        f'#define TAILSCALE_HOST   "{c_escape(values.get("TAILSCALE_HOST", ""))}"',
        "",
        "// Tailscale Subnet Router Watchdog configuration.",
        "// If TAILSCALE_API_KEY is empty, the ESP32 directly connects to Tailscale at boot (failover disabled).",
        "// Generate a read-only API access token at: https://login.tailscale.com/admin/settings/keys",
        f'#define TAILSCALE_API_KEY              "{c_escape(values.get("TAILSCALE_API_KEY", ""))}"',
        "",
        "// Primary subnet router device identifier: either numeric machine ID or hostname (e.g. \"moto-g32\")",
        f'#define TAILSCALE_SUBNET_DEVICE_ID     "{c_escape(values.get("TAILSCALE_SUBNET_DEVICE_ID", ""))}"',
        "",
        "// OTA Security Key (passphrase or PIN required for Over-The-Air firmware updates)",
        f'#define OTA_KEY          "{c_escape(values.get("OTA_KEY", ""))}"',
        "",
    ]
    return "\n".join(lines)


def print_summary(values: dict, show_raw: bool = False):
    """Print formatted summary table."""
    print(f"\n{CYAN}{BOX_TL}{BOX_H * INNER_WIDTH}{BOX_TR}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}{BOLD}{'CONFIGURATION REVIEW SUMMARY':^{INNER_WIDTH}}{RESET}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_LT}{BOX_H * 4}{BOX_TT}{BOX_H * 28}{BOX_TT}{BOX_H * 38}{BOX_RT}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET} {BOLD}#{RESET}  {CYAN}{BOX_V}{RESET} {BOLD}{'Setting':<26}{RESET} {CYAN}{BOX_V}{RESET} {BOLD}{'Configured Value':<36}{RESET} {CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_LT}{BOX_H * 4}{BOX_C}{BOX_H * 28}{BOX_C}{BOX_H * 38}{BOX_RT}{RESET}")

    for idx, field in enumerate(FIELDS, start=1):
        key = field["key"]
        val = values.get(key, "")

        if not val:
            if key in ("WIFI_PASSWORD", "OTA_KEY"):
                display_val = f"{DIM}(no password){RESET}"
                raw_len = 13
            else:
                display_val = f"{DIM}(disabled){RESET}"
                raw_len = 10
        elif field["is_secret"] and not show_raw:
            display_val = mask_value(val, key)
            raw_len = len(re.sub(r'\033\[[0-9;]*m', '', display_val))
        else:
            display_val = f"{GREEN}{val}{RESET}"
            raw_len = len(val)

        # Truncate visually if too wide
        if raw_len > 36:
            display_val = display_val[:33] + "..."
            pad = 0
        else:
            pad = 36 - raw_len

        print(f"{CYAN}{BOX_V}{RESET} {BOLD}{idx:<2}{RESET} {CYAN}{BOX_V}{RESET} {key:<26} {CYAN}{BOX_V}{RESET} {display_val}{' ' * pad} {CYAN}{BOX_V}{RESET}")

    print(f"{CYAN}{BOX_BL}{BOX_H * 4}{BOX_BT}{BOX_H * 28}{BOX_BT}{BOX_H * 38}{BOX_BR}{RESET}")


def main():
    print(f"\n{CYAN}{BOX_TL}{BOX_H * INNER_WIDTH}{BOX_TR}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}{BOLD}{CYAN}{'ESP32-SwitchBot : Interactive secrets.h Generator':^{INNER_WIDTH}}{RESET}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")
    print(f"  {GLYPH_INFO} Target destination : {BOLD}{TARGET_FILE}{RESET}")
    print(f"  {GLYPH_INFO} Step-by-step setup  : Confirm with {BOLD}[Y]{RESET} or retype with {BOLD}[n]{RESET}\n")

    # If file already exists, pre-load existing values
    existing_values = {}
    if TARGET_FILE.exists():
        print(f"  {YELLOW}{GLYPH_WARN} Existing secrets.h detected. Current values loaded as defaults.{RESET}")
        try:
            with open(TARGET_FILE, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    line = line.strip()
                    for field in FIELDS:
                        macro_prefix = f'#define {field["key"]}'
                        if line.startswith(macro_prefix):
                            rest = line[len(macro_prefix):].strip()
                            if rest.startswith('"') and rest.endswith('"'):
                                existing_values[field["key"]] = rest[1:-1]
        except Exception:
            pass

    values = {}
    total = len(FIELDS)

    # Step 1: Collect values sequentially
    for idx, field in enumerate(FIELDS, start=1):
        key = field["key"]

        # If TAILSCALE_KEY was not given, skip TAILSCALE_HOST
        if key == "TAILSCALE_HOST" and not values.get("TAILSCALE_KEY"):
            values[key] = ""
            print(f"\n  {DIM}{GLYPH_INFO} Tailscale network disabled (no auth key). Skipping Step {idx} ({field['title']}).{RESET}")
            continue

        # If TAILSCALE_API_KEY was not given, skip TAILSCALE_SUBNET_DEVICE_ID
        if key == "TAILSCALE_SUBNET_DEVICE_ID" and not values.get("TAILSCALE_API_KEY"):
            values[key] = ""
            print(f"\n  {DIM}{GLYPH_INFO} Subnet router watchdog disabled (no API key). Skipping Step {idx} ({field['title']}).{RESET}")
            continue

        curr = existing_values.get(key, "")
        values[key] = prompt_field(field, step_num=idx, total_steps=total, current_val=curr if curr else None)

    # Step 2: Review and edit menu
    show_raw = False
    while True:
        print_summary(values, show_raw=show_raw)
        print(f"\n{BOLD}Actions:{RESET}")
        print(f"  {GREEN}[Y]{RESET}   Save configuration to {TARGET_FILE.name}")
        print(f"  {YELLOW}[1-{total}]{RESET} Edit / retype a specific setting")
        print(f"  {CYAN}[v]{RESET}   {'Hide raw secret values' if show_raw else 'View raw unmasked values'}")
        print(f"  {RED}[q]{RESET}   Cancel and exit without saving")

        print(f"\n{BOLD}{CYAN}{GLYPH_PROMPT} Choice: {RESET}", end="", flush=True)
        try:
            choice = read_single_key(allowed_keys=("y", "1", "2", "3", "4", "5", "6", "7", "v", "q"), default="y")
        except (KeyboardInterrupt, EOFError):
            print(f"\n\n{RED}{GLYPH_WARN} Cancelled by user.{RESET}\n")
            sys.exit(1)

        if choice == "y":
            break
        elif choice == "q":
            print(f"\n{YELLOW}{GLYPH_INFO} Exited without saving changes.{RESET}\n")
            sys.exit(0)
        elif choice == "v":
            show_raw = not show_raw
        elif choice in ("1", "2", "3", "4", "5", "6", "7"):
            target_idx = int(choice)
            target_field = FIELDS[target_idx - 1]
            target_key = target_field["key"]

            if target_key == "TAILSCALE_HOST" and not values.get("TAILSCALE_KEY"):
                print(f"\n  {YELLOW}{GLYPH_WARN} Tailscale Hostname requires a Tailscale Auth Key.{RESET}")
                print(f"  {DIM}Please configure setting [3] (TAILSCALE_KEY) first.{RESET}\n")
                continue

            if target_key == "TAILSCALE_SUBNET_DEVICE_ID" and not values.get("TAILSCALE_API_KEY"):
                print(f"\n  {YELLOW}{GLYPH_WARN} Subnet router watchdog requires a Tailscale API Key.{RESET}")
                print(f"  {DIM}Please configure setting [5] (TAILSCALE_API_KEY) first.{RESET}\n")
                continue

            values[target_key] = prompt_field(
                target_field,
                step_num=target_idx,
                total_steps=total,
                current_val=values.get(target_key),
            )

            if target_key == "TAILSCALE_KEY":
                if not values["TAILSCALE_KEY"]:
                    if values.get("TAILSCALE_HOST"):
                        values["TAILSCALE_HOST"] = ""
                        print(f"\n  {DIM}{GLYPH_INFO} TAILSCALE_KEY cleared. Tailscale Hostname has been disabled.{RESET}")
                else:
                    if not values.get("TAILSCALE_HOST"):
                        print(f"\n  {CYAN}{GLYPH_INFO} TAILSCALE_KEY configured. Please specify the Tailscale Hostname.{RESET}")
                        host_field = FIELDS[3]
                        values["TAILSCALE_HOST"] = prompt_field(
                            host_field,
                            step_num=4,
                            total_steps=total,
                            current_val=existing_values.get("TAILSCALE_HOST") or "esp32",
                        )

            if target_key == "TAILSCALE_API_KEY":
                if not values["TAILSCALE_API_KEY"]:
                    if values.get("TAILSCALE_SUBNET_DEVICE_ID"):
                        values["TAILSCALE_SUBNET_DEVICE_ID"] = ""
                        print(f"\n  {DIM}{GLYPH_INFO} TAILSCALE_API_KEY cleared. Subnet Device ID has been disabled.{RESET}")
                else:
                    if not values.get("TAILSCALE_SUBNET_DEVICE_ID"):
                        print(f"\n  {CYAN}{GLYPH_INFO} TAILSCALE_API_KEY configured. Please specify the Primary Subnet Device ID.{RESET}")
                        subnet_field = FIELDS[5]
                        values["TAILSCALE_SUBNET_DEVICE_ID"] = prompt_field(
                            subnet_field,
                            step_num=6,
                            total_steps=total,
                            current_val=existing_values.get("TAILSCALE_SUBNET_DEVICE_ID"),
                        )

    # Step 3: Write file safely
    TARGET_FILE.parent.mkdir(parents=True, exist_ok=True)

    if TARGET_FILE.exists():
        backup_path = TARGET_FILE.with_suffix(".h.bak")
        try:
            shutil.copyfile(TARGET_FILE, backup_path)
            print(f"\n  {DIM}{GLYPH_INFO} Created backup: {backup_path}{RESET}")
        except Exception as e:
            print(f"\n  {YELLOW}{GLYPH_WARN} Could not create backup: {e}{RESET}")

    content = generate_header_content(values)
    try:
        with open(TARGET_FILE, "w", encoding="utf-8") as f:
            f.write(content)

        print(f"\n{GREEN}{BOX_TL}{BOX_H * INNER_WIDTH}{BOX_TR}{RESET}")
        print(f"{GREEN}{BOX_V}{RESET}{BOLD}{GREEN}{'SETUP COMPLETE':^{INNER_WIDTH}}{RESET}{GREEN}{BOX_V}{RESET}")
        print(f"{GREEN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")
        print(f"  {GREEN}{GLYPH_CHECK}{RESET} Generated header file : {BOLD}{TARGET_FILE}{RESET}")
        print(f"  {GREEN}{GLYPH_CHECK}{RESET} Ready to build project : {BOLD}idf.py build{RESET}\n")
    except Exception as e:
        print(f"\n{RED}{GLYPH_WARN} Error writing to {TARGET_FILE}: {e}{RESET}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()
