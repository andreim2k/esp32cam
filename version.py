import datetime
import os

VERSION_FILE = 'version.txt'
HEADER_FILE = 'src/version.h'

def get_next_version():
    now = datetime.datetime.now()
    date_str = now.strftime("%Y.%m.%d")
    
    current_build_no = 1
    last_date = ""
    
    if os.path.exists(VERSION_FILE):
        with open(VERSION_FILE, 'r') as f:
            try:
                content = f.read().strip()
                if '.' in content:
                    parts = content.split('.')
                    last_date = ".".join(parts[:3])
                    last_build_no = int(parts[3])
                    
                    if last_date == date_str:
                        current_build_no = last_build_no + 1
            except:
                pass
                
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
