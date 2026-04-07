#!/usr/bin/env python3
import argparse
import os
import re
from pathlib import Path

DEFAULT_DESKTOP_INVENTORY = Path.home() / 'Desktop' / 'esp32-hardware-overview.md'

ENTRY_TEMPLATE = '''## ESP32 Device on {port}

- **Chip:** {chip}
- **Revision:** {revision}
- **CPU:** {cpu}
- **Max. Takt:** {clock}
- **Funk:** {features}
- **Crystal-Frequenz:** {crystal}
- **On-Chip SRAM:** {sram}
- **PSRAM:** {psram}
- **Flash:** {flash}
- **MAC-Adresse:** {mac}
- **USB-Seriell:** {port}
- **Projekt:** {project}

'''

MAC_RE = re.compile(r"^\s*-\s*\*\*MAC-Adresse:\*\*\s*([0-9a-f:]+)", re.I)
PROJECT_RE = re.compile(r"^\s*-\s*\*\*Projekt:\*\*\s*(.+)", re.I)


def normalize_mac(mac: str) -> str:
    return mac.strip().lower().replace('-', ':')


def find_mac_section(lines, target_mac):
    target_mac = normalize_mac(target_mac)
    section_start = None
    section_end = None
    current_mac = None

    for i, line in enumerate(lines):
        match = MAC_RE.match(line)
        if match:
            current_mac = normalize_mac(match.group(1))
            if current_mac == target_mac:
                section_start = i
                # find actual start of section header above
                for j in range(i - 1, -1, -1):
                    if lines[j].startswith('## '):
                        section_start = j
                        break
                continue
        if section_start is not None and line.startswith('## ') and i > section_start:
            section_end = i
            break

    if section_start is not None and section_end is None:
        section_end = len(lines)
    return section_start, section_end


def update_inventory_file(path: Path, entry: dict) -> bool:
    if not path.exists():
        path.write_text('# ESP32 Hardware Übersicht\n\n')

    text = path.read_text(encoding='utf-8')
    lines = text.splitlines(keepends=True)
    section_start, section_end = find_mac_section(lines, entry['mac'])

    if section_start is not None:
        section = lines[section_start:section_end]
        for i, line in enumerate(section):
            if PROJECT_RE.match(line):
                project_line = f"- **Projekt:** {entry['project']}\n"
                if section_start + i < len(lines):
                    lines[section_start + i] = project_line
                else:
                    lines.insert(section_end, project_line)
                path.write_text(''.join(lines), encoding='utf-8')
                return True
        insert_pos = section_end
        lines.insert(insert_pos, f"- **Projekt:** {entry['project']}\n")
        lines.insert(insert_pos + 1, '\n')
        path.write_text(''.join(lines), encoding='utf-8')
        return True

    # append new entry if MAC not found
    with path.open('a', encoding='utf-8') as f:
        f.write(ENTRY_TEMPLATE.format(**entry))
    return False


def main():
    parser = argparse.ArgumentParser(description='Update ESP32 inventory by MAC address.')
    parser.add_argument('--mac', required=True, help='MAC address of the ESP32 device')
    parser.add_argument('--project', required=True, help='Project name to associate with the device')
    parser.add_argument('--port', default='COM?', help='Serial port where device was found')
    parser.add_argument('--chip', default='ESP32', help='Chip name')
    parser.add_argument('--revision', default='unknown', help='Chip revision')
    parser.add_argument('--cpu', default='Dual-core Xtensa + LP Core', help='CPU description')
    parser.add_argument('--clock', default='240 MHz', help='Crystal frequency or max clock')
    parser.add_argument('--features', default='Wi-Fi, Bluetooth', help='Radio/features')
    parser.add_argument('--crystal', default='40 MHz', help='Crystal frequency')
    parser.add_argument('--sram', default='520 KB', help='On-chip SRAM')
    parser.add_argument('--psram', default='unknown', help='PSRAM presence')
    parser.add_argument('--flash', default='4 MB SPI-Flash, 3.3V', help='Flash size/type')
    parser.add_argument('--inventory', default=str(DEFAULT_DESKTOP_INVENTORY), help='Inventory file path')
    args = parser.parse_args()

    inventory_path = Path(args.inventory)
    entry = {
        'mac': normalize_mac(args.mac),
        'project': args.project,
        'port': args.port,
        'chip': args.chip,
        'revision': args.revision,
        'cpu': args.cpu,
        'clock': args.clock,
        'features': args.features,
        'crystal': args.crystal,
        'sram': args.sram,
        'psram': args.psram,
        'flash': args.flash,
    }

    updated = update_inventory_file(inventory_path, entry)
    if updated:
        print(f"Updated existing inventory entry for MAC {entry['mac']} in {inventory_path}")
    else:
        print(f"Added new inventory entry for MAC {entry['mac']} to {inventory_path}")

    print(f"Inventory file: {inventory_path}")


if __name__ == '__main__':
    main()
