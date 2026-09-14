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

TAILSCALE_FIELDS = [
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
]

OTA_FIELD = {
    "key": "OTA_KEY",
    "category": "Device Security",
    "title": "OTA Security Password",
    "desc": "PIN or passphrase required to authorize /ota/enable updates.",
    "default": "",
    "required": False,
    "is_secret": True,
    "visible_prefix": "",
}



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


def validate_field(key: str, val: str) -> tuple:
    """Validate field input for length, format, and network protocol standards."""
    if not val:
        return True, ""

    if key == "WIFI_SSID":
        byte_len = len(val.encode("utf-8"))
        if byte_len > 32:
            return False, f"SSID is {byte_len} bytes (maximum allowed by 802.11 Wi-Fi standard is 32 bytes)."
        return True, ""

    elif key == "WIFI_PASSWORD":
        if len(val) < 8 or len(val) > 63:
            return False, f"WPA2 Wi-Fi password must be between 8 and 63 characters (currently {len(val)}). Clear it to use an open network."
        return True, ""

    elif key == "TAILSCALE_KEY":
        if not val.startswith("tskey-auth-") and len(val) < 15:
            return False, "Tailscale auth key must start with 'tskey-auth-' or be a valid pre-auth key."
        return True, ""

    elif key == "TAILSCALE_HOST":
        if not re.match(r'^[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?$', val):
            return False, "Hostname must be 1-63 alphanumeric characters and hyphens, and cannot start or end with a hyphen (RFC 1123)."
        return True, ""

    elif key == "TAILSCALE_API_KEY":
        if not val.startswith("tskey-api-") and len(val) < 15:
            return False, "Tailscale API token must start with 'tskey-api-' or be a valid access token."
        return True, ""

    elif key == "TAILSCALE_SUBNET_DEVICE_ID":
        if " " in val or len(val) > 64:
            return False, "Subnet device ID / hostname cannot contain spaces or exceed 64 characters."
        return True, ""

    elif key == "OTA_KEY":
        if len(val) > 64:
            return False, "OTA key cannot exceed 64 characters."
        return True, ""

    return True, ""


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

        # Validate entered value
        is_valid, err_msg = validate_field(key, val)
        if not is_valid:
            print(f"\n  {RED}{GLYPH_WARN} Invalid input: {err_msg}{RESET}")
            continue

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


def prompt_wifi_network(idx: int, existing: dict = None) -> dict:
    """Prompt for a Wi-Fi network's SSID and password."""
    is_primary = (idx == 1)
    title_suffix = "(Primary)" if is_primary else f"(Fallback #{idx})"
    category = f"Wi-Fi Network #{idx}"

    field_ssid = {
        "key": f"WIFI_SSID_{idx}",
        "category": category,
        "title": f"Wi-Fi SSID {title_suffix}",
        "desc": "Main home Wi-Fi network." if is_primary else f"Fallback Wi-Fi network #{idx} used if previous networks fail.",
        "default": "",
        "required": is_primary,
        "is_secret": False,
        "visible_prefix": "",
    }

    curr_ssid = existing.get("ssid", "") if existing else ""
    ssid_val = prompt_field(field_ssid, step_num=idx, total_steps=6, current_val=curr_ssid)
    if not ssid_val and not is_primary:
        return None

    field_pass = {
        "key": f"WIFI_PASSWORD_{idx}",
        "category": category,
        "title": f"Wi-Fi Password {title_suffix}",
        "desc": f"WPA/WPA2 passphrase for '{ssid_val}'. Leave empty for open network.",
        "default": "",
        "required": False,
        "is_secret": True,
        "visible_prefix": "",
    }
    curr_pass = existing.get("password", "") if existing else ""
    pass_val = prompt_field(field_pass, step_num=idx, total_steps=6, current_val=curr_pass)

    return {"ssid": ssid_val, "password": pass_val}


def manage_wifi_menu(wifi_networks: list) -> list:
    """Sub-menu to manage Wi-Fi networks in review stage."""
    while True:
        print(f"\n{CYAN}{BOX_TL}{BOX_H * INNER_WIDTH}{BOX_TR}{RESET}")
        print(f"{CYAN}{BOX_V}{RESET}{BOLD}{'MANAGE WI-FI NETWORKS':^{INNER_WIDTH}}{RESET}{CYAN}{BOX_V}{RESET}")
        print(f"{CYAN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")

        for i, net in enumerate(wifi_networks, start=1):
            tag = "Primary" if i == 1 else f"Fallback #{i}"
            pwd_disp = mask_value(net.get("password", ""), "WIFI_PASSWORD")
            clean_pwd = re.sub(r'\033\[[0-9;]*m', '', pwd_disp)
            print(f"  {BOLD}[{i}]{RESET} {tag:<14}: {BOLD}{net.get('ssid', '')}{RESET} {DIM}({clean_pwd}){RESET}")

        print(f"\n{BOLD}Options:{RESET}")
        print(f"  {YELLOW}[1-{len(wifi_networks)}]{RESET} Retype / edit specific network")
        if len(wifi_networks) < 6:
            print(f"  {GREEN}[a]{RESET}   Add another fallback network ({len(wifi_networks) + 1}/6)")
        if len(wifi_networks) > 1:
            print(f"  {RED}[d]{RESET}   Remove last fallback network (#{len(wifi_networks)})")
        print(f"  {CYAN}[b]{RESET}   Return to main review menu")

        print(f"\n{BOLD}{CYAN}{GLYPH_PROMPT} Choice: {RESET}", end="", flush=True)
        try:
            ch = read_single_key().lower()
        except (KeyboardInterrupt, EOFError):
            break

        if ch in ("b", "q", "\r", "\n", ""):
            break
        elif ch == "a" and len(wifi_networks) < 6:
            slot = len(wifi_networks) + 1
            net = prompt_wifi_network(slot)
            if net and net.get("ssid"):
                wifi_networks.append(net)
                print(f"\n  {GREEN}{GLYPH_CHECK} Added fallback network #{slot}: {net['ssid']}{RESET}")
        elif ch == "d" and len(wifi_networks) > 1:
            removed = wifi_networks.pop()
            print(f"\n  {YELLOW}{GLYPH_INFO} Removed fallback network: {removed['ssid']}{RESET}")
        elif ch.isdigit():
            idx = int(ch)
            if 1 <= idx <= len(wifi_networks):
                net = prompt_wifi_network(idx, existing=wifi_networks[idx - 1])
                if net and net.get("ssid"):
                    wifi_networks[idx - 1] = net

    return wifi_networks


def generate_header_content(wifi_networks: list, values: dict, fully_local: bool) -> str:
    """Generate the C++ header file content."""
    lines = [
        "#pragma once",
        "",
        "// Operation Mode",
        "// Set to 1 to run purely locally (skips Microlink & Tailscale completely)",
        f"#define FULLY_LOCAL_MODE                 {1 if fully_local else 0}",
        "",
        "// Wi-Fi Credentials & Failover Configuration (1 to 6 networks)",
        f"#define WIFI_NETWORK_COUNT               {len(wifi_networks)}",
    ]
    for i in range(1, 7):
        if i <= len(wifi_networks):
            net = wifi_networks[i - 1]
            lines.append(f'#define WIFI_SSID_{i}                      "{c_escape(net.get("ssid", ""))}"')
            lines.append(f'#define WIFI_PASSWORD_{i}                  "{c_escape(net.get("password", ""))}"')
        else:
            lines.append(f'#define WIFI_SSID_{i}                      ""')
            lines.append(f'#define WIFI_PASSWORD_{i}                  ""')

    main_ssid = wifi_networks[0]["ssid"] if wifi_networks else ""
    main_pass = wifi_networks[0]["password"] if wifi_networks else ""
    lines.extend([
        "",
        "// Backward compatibility macros",
        f'#define WIFI_SSID                        "{c_escape(main_ssid)}"',
        f'#define WIFI_PASSWORD                    "{c_escape(main_pass)}"',
        "",
        "// Tailscale Configuration",
        f'#define TAILSCALE_KEY                    "{c_escape(values.get("TAILSCALE_KEY", "") if not fully_local else "")}"',
        f'#define TAILSCALE_HOST                   "{c_escape(values.get("TAILSCALE_HOST", "esp32") if not fully_local else "")}"',
        "",
        "// Tailscale Subnet Router Watchdog configuration.",
        "// If TAILSCALE_API_KEY is empty, the ESP32 directly connects to Tailscale at boot (failover disabled).",
        "// Generate a read-only API access token at: https://login.tailscale.com/admin/settings/keys",
        f'#define TAILSCALE_API_KEY                "{c_escape(values.get("TAILSCALE_API_KEY", "") if not fully_local else "")}"',
        "",
        "// Primary subnet router device identifier: either numeric machine ID or hostname (e.g. \"moto-g32\")",
        f'#define TAILSCALE_SUBNET_DEVICE_ID       "{c_escape(values.get("TAILSCALE_SUBNET_DEVICE_ID", "") if not fully_local else "")}"',
        "",
        "// OTA Security Key (passphrase or PIN required for Over-The-Air firmware updates)",
        f'#define OTA_KEY                          "{c_escape(values.get("OTA_KEY", ""))}"',
        "",
    ])
    return "\n".join(lines)


def print_summary(wifi_networks: list, values: dict, fully_local: bool, show_raw: bool = False):
    """Print formatted summary table."""
    print(f"\n{CYAN}{BOX_TL}{BOX_H * INNER_WIDTH}{BOX_TR}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}{BOLD}{'CONFIGURATION REVIEW SUMMARY':^{INNER_WIDTH}}{RESET}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_LT}{BOX_H * 4}{BOX_TT}{BOX_H * 28}{BOX_TT}{BOX_H * 38}{BOX_RT}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET} {BOLD}#{RESET}  {CYAN}{BOX_V}{RESET} {BOLD}{'Setting':<26}{RESET} {CYAN}{BOX_V}{RESET} {BOLD}{'Configured Value':<36}{RESET} {CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_LT}{BOX_H * 4}{BOX_C}{BOX_H * 28}{BOX_C}{BOX_H * 38}{BOX_RT}{RESET}")

    rows = []
    # Operation mode
    mode_text = f"{CYAN}Fully Local (No Tailscale){RESET}" if fully_local else f"{GREEN}Tailscale Enabled{RESET}"
    rows.append(("Mode", "Operation Mode", mode_text, "Fully Local (No Tailscale)" if fully_local else "Tailscale Enabled"))

    # Wi-Fi networks
    for i, net in enumerate(wifi_networks, start=1):
        lbl = f"Wi-Fi #{i} (Primary)" if i == 1 else f"Wi-Fi #{i} (Fallback)"
        ssid = net.get("ssid", "")
        pwd = net.get("password", "")
        if show_raw:
            val_display = f"{ssid} [pwd: {pwd if pwd else '(none)'}]"
            clean_len = len(val_display)
        else:
            if pwd:
                pwd_masked = mask_value(pwd, "WIFI_PASSWORD")
                clean_pwd = re.sub(r'\033\[[0-9;]*m', '', pwd_masked)
                val_display = f"{ssid} {DIM}(pass set){RESET}"
                clean_len = len(ssid) + 11
            else:
                val_display = f"{ssid} {DIM}(open){RESET}"
                clean_len = len(ssid) + 7
        rows.append((f"W{i}", lbl, val_display, clean_len))

    # Tailscale settings
    if fully_local:
        rows.append(("-", "Tailscale Network", f"{DIM}(bypassed - local mode){RESET}", 23))
    else:
        for f in TAILSCALE_FIELDS:
            k = f["key"]
            val = values.get(k, "")
            if not val:
                disp = f"{DIM}(disabled){RESET}"
                rlen = 10
            elif f["is_secret"] and not show_raw:
                disp = mask_value(val, k)
                rlen = len(re.sub(r'\033\[[0-9;]*m', '', disp))
            else:
                disp = f"{GREEN}{val}{RESET}"
                rlen = len(val)
            rows.append((k, f["title"], disp, rlen))

    # OTA setting
    ota_val = values.get("OTA_KEY", "")
    if not ota_val:
        ota_disp = f"{DIM}(no password){RESET}"
        ota_len = 13
    elif not show_raw:
        ota_disp = mask_value(ota_val, "OTA_KEY")
        ota_len = len(re.sub(r'\033\[[0-9;]*m', '', ota_disp))
    else:
        ota_disp = f"{GREEN}{ota_val}{RESET}"
        ota_len = len(ota_val)
    rows.append(("OTA", "OTA Security Password", ota_disp, ota_len))


    # Print rows
    for idx_num, (tag, title, disp_val, raw_len_val) in enumerate(rows, start=1):
        if isinstance(raw_len_val, str):
            raw_len = len(re.sub(r'\033\[[0-9;]*m', '', raw_len_val))
        else:
            raw_len = raw_len_val
        if raw_len > 36:
            disp_val = disp_val[:33] + "..."
            pad = 0
        else:
            pad = 36 - raw_len
        print(f"{CYAN}{BOX_V}{RESET} {BOLD}{idx_num:<2}{RESET} {CYAN}{BOX_V}{RESET} {title:<26} {CYAN}{BOX_V}{RESET} {disp_val}{' ' * max(0, pad)} {CYAN}{BOX_V}{RESET}")

    print(f"{CYAN}{BOX_BL}{BOX_H * 4}{BOX_BT}{BOX_H * 28}{BOX_BT}{BOX_H * 38}{BOX_BR}{RESET}")


def main():
    print(f"\n{CYAN}{BOX_TL}{BOX_H * INNER_WIDTH}{BOX_TR}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}{BOLD}{CYAN}{'ESP32-SwitchBot : Interactive secrets.h Generator':^{INNER_WIDTH}}{RESET}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")
    print(f"  {GLYPH_INFO} Target destination : {BOLD}{TARGET_FILE}{RESET}")
    print(f"  {GLYPH_INFO} Step-by-step setup  : Confirm with {BOLD}[Y]{RESET} or retype with {BOLD}[n]{RESET}\n")

    # Load existing secrets if file exists
    existing_wifi = []
    existing_values = {}
    existing_local = False

    if TARGET_FILE.exists():
        print(f"  {YELLOW}{GLYPH_WARN} Existing secrets.h detected. Current values loaded as defaults.{RESET}")
        try:
            with open(TARGET_FILE, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()

            local_m = re.search(r'#define\s+FULLY_LOCAL_MODE\s+([01])', content)
            if local_m:
                existing_local = (local_m.group(1) == "1")

            for i in range(1, 7):
                ssid_m = re.search(rf'#define\s+WIFI_SSID_{i}\s+"([^"]*)"', content)
                pass_m = re.search(rf'#define\s+WIFI_PASSWORD_{i}\s+"([^"]*)"', content)
                if ssid_m and ssid_m.group(1):
                    existing_wifi.append({"ssid": ssid_m.group(1), "password": pass_m.group(1) if pass_m else ""})

            if not existing_wifi:
                old_ssid_m = re.search(r'#define\s+WIFI_SSID\s+"([^"]*)"', content)
                old_pass_m = re.search(r'#define\s+WIFI_PASSWORD\s+"([^"]*)"', content)
                if old_ssid_m and old_ssid_m.group(1):
                    existing_wifi.append({"ssid": old_ssid_m.group(1), "password": old_pass_m.group(1) if old_pass_m else ""})

            for key in ("TAILSCALE_KEY", "TAILSCALE_HOST", "TAILSCALE_API_KEY", "TAILSCALE_SUBNET_DEVICE_ID", "OTA_KEY"):
                km = re.search(rf'#define\s+{key}\s+"([^"]*)"', content)
                if km:
                    existing_values[key] = km.group(1)
        except Exception:
            pass

    # Step 1: Wi-Fi Setup
    print(f"\n{CYAN}{BOX_TL}{BOX_H}{BOLD} [Step 1] Wi-Fi Network Setup {RESET}{CYAN}{BOX_H * (BOX_WIDTH - 2 - 1 - 30)}{BOX_TR}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}  {BOLD}Configuring Primary & Fallback Wi-Fi Networks{RESET}{' ' * (INNER_WIDTH - 47)}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}  {DIM}Network #1 is mandatory. Up to 5 additional fallback networks optional.{RESET}{' ' * (INNER_WIDTH - 73)}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")

    net1 = prompt_wifi_network(1, existing=existing_wifi[0] if existing_wifi else None)
    wifi_networks = [net1]

    for slot in range(2, 7):
        print(f"\n  {CYAN}{GLYPH_PROMPT} Do you want to add another Wi-Fi network (fallback)? [y/N]: {RESET}", end="", flush=True)
        try:
            ch = read_single_key(allowed_keys=("y", "n"), default="n")
        except (KeyboardInterrupt, EOFError):
            print(f"\n\n{RED}{GLYPH_WARN} Setup cancelled by user.{RESET}\n")
            sys.exit(1)

        if ch == "y":
            exist_slot = existing_wifi[slot - 1] if len(existing_wifi) >= slot else None
            net = prompt_wifi_network(slot, existing=exist_slot)
            if net and net.get("ssid"):
                wifi_networks.append(net)
                if len(wifi_networks) == 6:
                    print(f"\n  {GREEN}{GLYPH_CHECK} Maximum of 6 Wi-Fi networks configured. Moving to next step.{RESET}")
                    break
            else:
                print(f"\n  {DIM}{GLYPH_INFO} Skipped adding fallback network #{slot}.{RESET}")
        else:
            break

    # Step 2: Fully Local Mode Option
    header_text = " [Step 2] Operation Mode "
    pad_len = BOX_WIDTH - 2 - 1 - len(header_text)
    print(f"\n{CYAN}{BOX_TL}{BOX_H}{BOLD}{header_text}{RESET}{CYAN}{BOX_H * pad_len}{BOX_TR}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}  {BOLD}Local-Only vs. Tailscale Remote Access{RESET}{' ' * (INNER_WIDTH - 40)}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}  {DIM}Fully local mode disables Microlink & Tailscale, saving CPU and RAM.{RESET}{' ' * (INNER_WIDTH - 69)}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_V}{RESET}  {DIM}The ESP32 will only be accessed over local Wi-Fi or subnet router.{RESET}{' ' * (INNER_WIDTH - 68)}{CYAN}{BOX_V}{RESET}")
    print(f"{CYAN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")

    print(f"\n  {YELLOW}{GLYPH_PROMPT} Are you trying to setup this ESP fully local (no Tailscale)? [y/N]: {RESET}", end="", flush=True)
    try:
        local_ch = read_single_key(allowed_keys=("y", "n"), default="y" if existing_local else "n")
    except (KeyboardInterrupt, EOFError):
        print(f"\n\n{RED}{GLYPH_WARN} Setup cancelled by user.{RESET}\n")
        sys.exit(1)

    fully_local = (local_ch == "y")
    values = {}

    if fully_local:
        print(f"\n  {GREEN}{GLYPH_CHECK} Fully local mode selected. Tailscale queries skipped.{RESET}")
        values["TAILSCALE_KEY"] = ""
        values["TAILSCALE_HOST"] = ""
        values["TAILSCALE_API_KEY"] = ""
        values["TAILSCALE_SUBNET_DEVICE_ID"] = ""
    else:
        print(f"\n  {CYAN}{GLYPH_INFO} Tailscale remote networking selected.{RESET}")
        for idx, field in enumerate(TAILSCALE_FIELDS, start=1):
            k = field["key"]
            if k == "TAILSCALE_HOST" and not values.get("TAILSCALE_KEY"):
                values[k] = ""
                print(f"\n  {DIM}{GLYPH_INFO} Tailscale network disabled (no auth key). Skipping {field['title']}.{RESET}")
                continue
            if k == "TAILSCALE_SUBNET_DEVICE_ID" and not values.get("TAILSCALE_API_KEY"):
                values[k] = ""
                print(f"\n  {DIM}{GLYPH_INFO} Subnet router watchdog disabled (no API key). Skipping {field['title']}.{RESET}")
                continue
            curr = existing_values.get(k, "")
            values[k] = prompt_field(field, step_num=idx, total_steps=len(TAILSCALE_FIELDS), current_val=curr if curr else None)

    # Step 3: OTA Security Password
    curr_ota = existing_values.get("OTA_KEY", "")
    values["OTA_KEY"] = prompt_field(OTA_FIELD, step_num=1, total_steps=1, current_val=curr_ota if curr_ota else None)

    # Step 4: Review and Edit Menu
    show_raw = False
    while True:
        print_summary(wifi_networks, values, fully_local=fully_local, show_raw=show_raw)
        print(f"\n{BOLD}Actions:{RESET}")
        print(f"  {GREEN}[Y]{RESET}   Save configuration to {TARGET_FILE.name}")
        print(f"  {YELLOW}[W]{RESET}   Manage Wi-Fi networks (add / edit / delete)")
        print(f"  {CYAN}[L]{RESET}   Toggle Operation Mode ({'Switch to Tailscale' if fully_local else 'Switch to Local Only'})")
        if not fully_local:
            print(f"  {YELLOW}[T]{RESET}   Edit Tailscale settings")
        print(f"  {YELLOW}[O]{RESET}   Edit OTA Security Password")
        print(f"  {CYAN}[v]{RESET}   {'Hide raw secret values' if show_raw else 'View raw unmasked values'}")
        print(f"  {RED}[q]{RESET}   Cancel and exit without saving")

        print(f"\n{BOLD}{CYAN}{GLYPH_PROMPT} Choice: {RESET}", end="", flush=True)
        try:
            choice = read_single_key(allowed_keys=("y", "w", "l", "t", "o", "v", "q"), default="y")
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
        elif choice == "w":
            wifi_networks = manage_wifi_menu(wifi_networks)
        elif choice == "l":
            fully_local = not fully_local
            if fully_local:
                print(f"\n  {GREEN}{GLYPH_CHECK} Switched to Fully Local mode.{RESET}")
            else:
                print(f"\n  {CYAN}{GLYPH_INFO} Switched to Tailscale remote mode.{RESET}")
                if not values.get("TAILSCALE_KEY"):
                    for idx, field in enumerate(TAILSCALE_FIELDS, start=1):
                        k = field["key"]
                        if k == "TAILSCALE_HOST" and not values.get("TAILSCALE_KEY"):
                            values[k] = ""
                            continue
                        if k == "TAILSCALE_SUBNET_DEVICE_ID" and not values.get("TAILSCALE_API_KEY"):
                            values[k] = ""
                            continue
                        values[k] = prompt_field(field, step_num=idx, total_steps=len(TAILSCALE_FIELDS), current_val=values.get(k))
        elif choice == "t" and not fully_local:
            for idx, field in enumerate(TAILSCALE_FIELDS, start=1):
                k = field["key"]
                values[k] = prompt_field(field, step_num=idx, total_steps=len(TAILSCALE_FIELDS), current_val=values.get(k))
        elif choice == "o":
            values["OTA_KEY"] = prompt_field(OTA_FIELD, step_num=1, total_steps=1, current_val=values.get("OTA_KEY"))

    # Step 5: Write file safely
    TARGET_FILE.parent.mkdir(parents=True, exist_ok=True)

    if TARGET_FILE.exists():
        backup_path = TARGET_FILE.with_suffix(".h.bak")
        try:
            shutil.copyfile(TARGET_FILE, backup_path)
            print(f"\n  {DIM}{GLYPH_INFO} Created backup: {backup_path}{RESET}")
        except Exception as e:
            print(f"\n  {YELLOW}{GLYPH_WARN} Could not create backup: {e}{RESET}")

    content = generate_header_content(wifi_networks, values, fully_local=fully_local)
    try:
        with open(TARGET_FILE, "w", encoding="utf-8") as f:
            f.write(content)

        print(f"\n{GREEN}{BOX_TL}{BOX_H * INNER_WIDTH}{BOX_TR}{RESET}")
        print(f"{GREEN}{BOX_V}{RESET}{BOLD}{GREEN}{'SETUP COMPLETE':^{INNER_WIDTH}}{RESET}{GREEN}{BOX_V}{RESET}")
        print(f"{GREEN}{BOX_BL}{BOX_H * INNER_WIDTH}{BOX_BR}{RESET}")
        print(f"  {GREEN}{GLYPH_CHECK}{RESET} Generated header file : {BOLD}{TARGET_FILE}{RESET}")
        print(f"  {GREEN}{GLYPH_CHECK}{RESET} Mode                  : {BOLD}{'Fully Local (No Tailscale)' if fully_local else 'Tailscale Enabled'}{RESET}")
        print(f"  {GREEN}{GLYPH_CHECK}{RESET} Wi-Fi Networks        : {BOLD}{len(wifi_networks)} configured{RESET}")
        print(f"  {GREEN}{GLYPH_CHECK}{RESET} Ready to build project : {BOLD}idf.py build{RESET}\n")
    except Exception as e:
        print(f"\n{RED}{GLYPH_WARN} Error writing to {TARGET_FILE}: {e}{RESET}\n")
        sys.exit(1)


if __name__ == "__main__":
    main()

