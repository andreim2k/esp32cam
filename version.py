import datetime
import os

VERSION_FILE = 'version.txt'
HEADER_FILE = 'src/version.h'
PRIMARY_ENV = os.getenv("PRIMARY_BUILD_ENV", "esp32cam")

CURRENT_ENV = os.getenv("PIOENV", "")
try:
    Import("env")
    CURRENT_ENV = env.get("PIOENV", CURRENT_ENV)
except Exception:
    pass

def parse_version(content):
    parts = content.strip().split('.')
    if len(parts) != 4:
        return None, None
    try:
        return ".".join(parts[:3]), int(parts[3])
    except ValueError:
        return None, None

def read_current_version():
    if not os.path.exists(VERSION_FILE):
        return None, None
    with open(VERSION_FILE, 'r') as f:
        return parse_version(f.read())

def get_next_version():
    now = datetime.datetime.now()
    date_str = now.strftime("%Y.%m.%d")

    current_build_no = 1
    last_date, last_build_no = read_current_version()

    # Secondary environments (e.g. OTA) reuse the already generated version for
    # the day to keep multi-env pio runs on a single build number.
    if CURRENT_ENV and CURRENT_ENV != PRIMARY_ENV:
        if last_date == date_str and last_build_no is not None:
            return f"{date_str}.{last_build_no}", now.strftime("%Y-%m-%d %H:%M:%S")
        return f"{date_str}.1", now.strftime("%Y-%m-%d %H:%M:%S")

    if last_date == date_str and last_build_no is not None:
        current_build_no = last_build_no + 1

    version_str = f"{date_str}.{current_build_no}"
    return version_str, now.strftime("%Y-%m-%d %H:%M:%S")

def save_version(version):
    with open(VERSION_FILE, 'w') as f:
        f.write(version)

version_str, timestamp = get_next_version()
save_version(version_str)

with open(HEADER_FILE, 'w') as f:
    f.write(f'#ifndef VERSION_H\n')
    f.write(f'#define VERSION_H\n\n')
    f.write(f'#define BUILD_NUMBER "{version_str}"\n')
    f.write(f'#define BUILD_TIMESTAMP "{timestamp}"\n\n')
    f.write(f'#endif // VERSION_H\n')

print(f'Build Version: {version_str}')
print(f'Build Timestamp: {timestamp}')
