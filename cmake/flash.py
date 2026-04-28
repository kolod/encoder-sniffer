#!/usr/bin/env python3
"""
Flash RP2040 firmware after a successful build.

Steps:
  1. If the RPI-RP2 mass-storage drive is already visible, copy directly.
  2. Otherwise find the CDC serial port, send 'boot', then wait for the drive.
  3. Copy the UF2 to the drive.

Usage:
  flash.py [--port <COM3>] [--uid <hex>] [--verbose] <firmware.uf2>

--uid  targets a specific board by its unique ID (output of the 'uid' serial
       command).  Accepts plain hex or colon/dash-separated bytes, e.g.:
         e6614c311b812b23
         e6:61:4c:31:1b:81:2b:23

If no device is reachable at all, exit 0 so the build is not marked failed.
Requires: pyserial  (pip install pyserial)
          rich      (pip install rich)
"""

from sys import platform, exit
from string import ascii_uppercase
from time import sleep, monotonic
from argparse import ArgumentParser
from pathlib import Path
from rich.console import Console
from rich.status  import Status


class Flash:
    VOLUME_LABEL = "RPI-RP2"
    RP2040_VID   = 0x2E8A
    BOOT_TIMEOUT = 10   # seconds to wait for the drive after sending boot command

    def __init__(self, uf2: Path, port: str | None = None,
                 uid: str | None = None, verbose: bool = False) -> None:
        self.uf2     = uf2
        self.port    = port
        self.uid     = uid
        self.verbose = verbose
        self.console = Console(highlight=False)

    # ---- Helpers ----------------------------------------------------------------

    def vprint(self, msg: str) -> None:
        if self.verbose:
            self.console.print(f"  [dim]{msg}[/dim]")

    @staticmethod
    def normalize_uid(uid: str) -> str:
        return uid.replace(":", "").replace("-", "").lower()

    # ---- Drive detection --------------------------------------------------------

    def find_drive(self) -> Path | None:
        if platform == "win32":
            import ctypes
            vol  = ctypes.create_unicode_buffer(256)
            mask = ctypes.windll.kernel32.GetLogicalDrives()
            for i, ch in enumerate(ascii_uppercase):
                if not (mask & (1 << i)):
                    continue
                drive = Path(f"{ch}:\\")
                ctypes.windll.kernel32.GetVolumeInformationW(
                    str(drive), vol, 256, None, None, None, None, 0)
                self.vprint(f"Drive {drive}: label='{vol.value}'")
                if vol.value == self.VOLUME_LABEL:
                    return drive
        elif platform == "darwin":
            p = Path(f"/Volumes/{self.VOLUME_LABEL}")
            self.vprint(f"Checking {p}")
            if p.is_dir():
                return p
        else:
            for base in (f"/media/{Path('/etc/passwd').stat().st_uid}", "/media", "/mnt"):
                p = Path(base) / self.VOLUME_LABEL
                self.vprint(f"Checking {p}")
                if p.is_dir():
                    return p
        return None

    def wait_for_drive(self) -> Path | None:
        deadline = monotonic() + self.BOOT_TIMEOUT
        with Status(
            f"  Waiting for [cyan]{self.VOLUME_LABEL}[/cyan] drive "
            f"(up to {self.BOOT_TIMEOUT}s) ...",
            console=self.console, spinner="dots",
        ):
            while monotonic() < deadline:
                drive = self.find_drive()
                if drive:
                    return drive
                sleep(0.25)
        return None

    # ---- Serial port ------------------------------------------------------------

    def list_ports(self):
        try:
            from serial.tools.list_ports import comports
            return list(comports())
        except ImportError:
            return []

    def auto_find_port(self) -> str | None:
        for p in self.list_ports():
            vid_str = f"{p.vid:#06x}" if p.vid else "unknown"
            self.vprint(f"Port {p.device}: VID={vid_str} desc='{p.description}'")
            if p.vid == self.RP2040_VID:
                return p.device
        return None

    def query_uid(self, port_name: str) -> str | None:
        try:
            from serial import Serial
        except ImportError:
            return None
        try:
            with Serial(port_name, timeout=1) as s:
                s.reset_input_buffer()
                s.write(b"uid\r\n")
                deadline = monotonic() + 2.0
                while monotonic() < deadline:
                    line = s.readline().decode("ascii", errors="ignore").strip()
                    if line.startswith("uid="):
                        return line[4:].lower()
        except Exception as e:
            self.vprint(f"UID query failed on {port_name}: {e}")
        return None

    def find_port_by_uid(self, wanted_uid: str) -> str | None:
        wanted = self.normalize_uid(wanted_uid)
        for p in self.list_ports():
            vid_str = f"{p.vid:#06x}" if p.vid else "unknown"
            self.vprint(f"Port {p.device}: VID={vid_str}")
            if p.vid != self.RP2040_VID:
                continue
            self.vprint(f"  Querying UID from {p.device} ...")
            got = self.query_uid(p.device)
            self.vprint(f"  UID: {got}")
            if got and self.normalize_uid(got) == wanted:
                return p.device
        return None

    def send_boot(self, port_name: str) -> bool:
        try:
            from serial import Serial
        except ImportError:
            self.console.print("  [yellow]pyserial not installed — cannot send boot command[/yellow]")
            self.console.print("  [dim]Install with:  pip install pyserial[/dim]")
            return False
        try:
            self.vprint(f"Opening {port_name} ...")
            with Serial(port_name, timeout=2) as s:
                s.write(b"boot\r\n")
                sleep(0.3)
            self.console.print(f"  Sent [bold]boot[/bold] to [cyan]{port_name}[/cyan]")
            return True
        except Exception as e:
            self.console.print(f"  [red]Serial error on {port_name}: {e}[/red]")
            return False

    # ---- Resolve port -----------------------------------------------------------

    def resolve_port(self) -> str | None:
        if self.uid:
            if self.port:
                self.vprint(f"Verifying UID on {self.port} ...")
                got = self.query_uid(self.port)
                self.vprint(f"  UID: {got}")
                if got and self.normalize_uid(got) == self.normalize_uid(self.uid):
                    return self.port
                self.console.print(
                    f"  [yellow]Port {self.port} UID ({got}) does not match "
                    f"requested {self.uid} — skipping flash.[/yellow]")
                return None
            port = self.find_port_by_uid(self.uid)
            if port:
                self.console.print(f"  Found matching device on [cyan]{port}[/cyan]")
            else:
                self.console.print(
                    f"  [yellow]No RP2040 with UID {self.uid} found — skipping flash.[/yellow]")
            return port

        if self.port:
            self.vprint(f"Using explicitly specified port {self.port}")
            return self.port

        port = self.auto_find_port()
        if port:
            self.vprint(f"Auto-detected RP2040 port: {port}")
        return port

    # ---- Run --------------------------------------------------------------------

    def run(self) -> None:
        if not self.uf2.is_file():
            self.console.print(f"[bold red]ERROR:[/bold red] UF2 file not found: {self.uf2}")
            exit(1)

        self.console.print(f"\n[bold]Flashing[/bold] [cyan]{self.uf2.name}[/cyan] ...")

        drive = self.find_drive()

        if not drive:
            port = self.resolve_port()

            if not port:
                ports = self.list_ports()
                if ports:
                    names = ", ".join(f"[cyan]{p.device}[/cyan]" for p in ports)
                    self.console.print(f"  [yellow]No RP2040 port found by VID.[/yellow] Available: {names}")
                    self.console.print(f"  [dim]Use --port <name> to specify one explicitly.[/dim]")
                else:
                    self.console.print("  [dim]No device in BOOTSEL mode and no serial ports found — skipping flash.[/dim]")
                exit(0)

            uid = self.query_uid(port)
            if uid:
                self.console.print(f"  Device UID: [cyan]{uid}[/cyan]")

            if not self.send_boot(port):
                self.console.print("  [yellow]Could not trigger boot — skipping flash.[/yellow]")
                exit(0)

            drive = self.wait_for_drive()
            if not drive:
                self.console.print("  [yellow]Drive did not appear — skipping flash.[/yellow]")
                exit(0)

        sleep(0.5)  # Windows needs a moment after the drive letter appears

        dest = drive / self.uf2.name
        self.console.print(f"  Copying to [cyan]{drive} [/cyan]...")
        try:
            import shutil
            shutil.copy2(self.uf2, dest)
        except Exception as e:
            self.console.print(f"  [bold red]ERROR:[/bold red] Copy failed: {e}")
            exit(1)

        self.console.print("  [bold green]Done.[/bold green]\n")


# ---- Entry point ------------------------------------------------------------

def main() -> None:
    parser = ArgumentParser(description="Flash RP2040 UF2 firmware")
    parser.add_argument("uf2",  type=Path, help="Path to the .uf2 file")
    parser.add_argument("-p", "--port", metavar="PORT", help="Serial port (e.g. COM3). Auto-detected by VID if omitted.")
    parser.add_argument("-u", "--uid", metavar="UID", help="Target a specific board by its unique ID (hex, from 'uid' command).")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print diagnostic messages.")
    args = parser.parse_args()

    Flash(uf2=args.uf2, port=args.port, uid=args.uid, verbose=args.verbose).run()


if __name__ == "__main__":
    main()
