"""Test kd_installieren: installieren.sh und system-einrichten.sh (P15, Abschnitt c3).

Installiert mit den Optionen zum Prüfen in einen Ordner unter build/ (ohne Einheit, ohne ssh),
startet den Dienst aus dem Zielordner mit --attrappe und fragt GET status. HOME und XDG_* zeigen
in den Testordner, damit nichts die echte Installation trifft. system-einrichten.sh nur mit
bash -n und --trocken; firewall-cmd wird nie ausgeführt.

Aufruf: test_installieren.py <testordner>
"""

import os
import re
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_api import START_TIMEOUT_S, Service, free_port, ini, request  # noqa: E402

SERVICE_DIR = Path(__file__).resolve().parent.parent
INSTALL = SERVICE_DIR / "installieren.sh"
SYSTEM = SERVICE_DIR / "system-einrichten.sh"
FOLDER: Path = Path()
SCRIPT_TIMEOUT_S = 60


def env() -> dict:
    result = dict(os.environ)
    result["HOME"] = str(FOLDER / "home")
    result["XDG_CONFIG_HOME"] = str(FOLDER / "home" / "config")
    result["XDG_DATA_HOME"] = str(FOLDER / "home" / "daten")
    result["XDG_RUNTIME_DIR"] = str(FOLDER / "laufzeit")
    result["PYTHONDONTWRITEBYTECODE"] = "1"
    return result


def run(*args: str, extra_env: dict | None = None) -> subprocess.CompletedProcess:
    environment = env()
    environment.update(extra_env or {})
    return subprocess.run(["bash", *args], env=environment, cwd=FOLDER, capture_output=True,
                          text=True, timeout=SCRIPT_TIMEOUT_S)


class InstalledService(Service):
    """Dienst aus dem Zielordner statt aus dem Repo."""

    def __init__(self, folder: Path, ini_text: str, program: Path) -> None:
        super().__init__(folder, ini_text)
        self.program = program

    def start(self) -> None:
        import test_api
        saved = test_api.SERVICE_DIR
        test_api.SERVICE_DIR = self.program
        try:
            super().start()
        finally:
            test_api.SERVICE_DIR = saved


class InstallTest(unittest.TestCase):
    def setUp(self) -> None:
        self.target = FOLDER / "opt" / "dss-kamera"
        self.data = FOLDER / "daten" / "dss-kamera"
        self.backups = FOLDER / "daten" / "dss-kamera-sicherungen"
        for name in ("opt", "daten", "home", "laufzeit", "dienst"):
            shutil.rmtree(FOLDER / name, ignore_errors=True)
        (FOLDER / "home").mkdir()
        (FOLDER / "laufzeit").mkdir()

    def install(self, *extra: str, extra_env: dict | None = None) -> subprocess.CompletedProcess:
        return run(str(INSTALL), *extra, "--ziel", str(self.target), "--datenordner", str(self.data),
                   "--ohne-einheit", extra_env=extra_env)

    def test_install_twice_with_backup_and_start(self) -> None:
        first = self.install("--lokal")
        self.assertEqual(first.returncode, 0, first.stderr + first.stdout)
        self.assertIn("ohne Einheit", first.stdout)
        for name in ("dss_camera/__main__.py", "system-einrichten.sh", "dss-kamera.service",
                     "LIESMICH.md", "version.txt"):
            self.assertTrue((self.target / name).exists(), name)
        self.assertFalse((self.target / "installieren.sh").exists())
        self.assertEqual(list(self.target.rglob("__pycache__")), [])
        self.assertTrue(os.access(self.target / "system-einrichten.sh", os.X_OK))
        self.assertRegex((self.target / "version.txt").read_text(encoding="utf-8"),
                         r"^Kamera-Dienst \d+\.\d+\.\d+, Commit \S+")
        self.assertFalse(self.backups.exists(), "erster Lauf ohne Sicherung")
        # Nie eine Einheit, nie etwas unter HOME ausser dem, was der Test selbst anlegt.
        self.assertFalse((FOLDER / "home" / "config" / "systemd").exists())

        # Zweiter Lauf: Daten und eine Datei, die die neue Version nicht mitbringt.
        self.data.mkdir(parents=True)
        (self.data / "kameras.json").write_text('{"format_version": 1}\n', encoding="utf-8")
        (self.target / "dss_camera" / "alt.py").write_text("# alt\n", encoding="utf-8")
        second = self.install("--lokal")
        self.assertEqual(second.returncode, 0, second.stderr + second.stdout)
        backups = sorted(self.backups.iterdir())
        self.assertEqual(len(backups), 1)
        self.assertIn(f"Sicherung: {backups[0]}", second.stdout)
        self.assertRegex(backups[0].name, r"^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}")
        self.assertTrue((backups[0] / "programm" / "dss_camera" / "alt.py").exists())
        self.assertEqual((backups[0] / "daten" / "kameras.json").read_text(encoding="utf-8"),
                         '{"format_version": 1}\n')
        self.assertFalse((self.target / "dss_camera" / "alt.py").exists())
        self.assertEqual((self.data / "kameras.json").read_text(encoding="utf-8"),
                         '{"format_version": 1}\n')

        # Das Paket kommt aus dem Zielordner, nicht aus dem Repo.
        where = subprocess.run([sys.executable, "-c", "import dss_camera; print(dss_camera.__file__)"],
                               cwd=self.target, env=env(), capture_output=True, text=True,
                               timeout=SCRIPT_TIMEOUT_S)
        self.assertEqual(Path(where.stdout.strip()).parent, self.target / "dss_camera")

        port = free_port()
        (FOLDER / "dienst").mkdir()
        service = InstalledService(FOLDER / "dienst", ini(port), self.target)
        service.start()
        try:
            status, body = request(port, "/api/v1/status")
            self.assertEqual(status, 200)
            self.assertTrue(body["ok"])
            self.assertTrue(body["fake"])
        finally:
            self.assertEqual(service.stop(), 0, service.stderr)

    def test_over_ssh_without_network(self) -> None:
        # Ersatz für ssh: führt den Befehl auf diesem PC aus. Prüft Kopieren mit tar über ssh,
        # Weitergabe der Optionen und das Aufräumen des Ordners unter /tmp.
        bin_dir = FOLDER / "home" / "bin"
        bin_dir.mkdir()
        log = FOLDER / "home" / "ssh.log"
        fake_ssh = bin_dir / "ssh"
        fake_ssh.write_text(
            "#!/usr/bin/env bash\n"
            f"while [[ $# -gt 0 && $1 != -- ]]; do shift; done\nshift\n"
            f"echo \"$1 $2\" >> {log}\nshift\nexec bash -c \"$1\"\n", encoding="utf-8")
        fake_ssh.chmod(0o755)
        path = f"{bin_dir}:{os.environ.get('PATH', '')}"
        result = self.install("127.0.0.1", extra_env={"PATH": path})
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertTrue((self.target / "dss_camera" / "__main__.py").exists())
        calls = log.read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(calls), 3, calls)
        self.assertTrue(all(call.startswith("127.0.0.1 ") for call in calls))
        remote = re.search(r"/tmp/dss-kamera-neu\.[A-Za-z0-9]+", calls[1]).group(0)
        self.assertFalse(Path(remote).exists(), "Ordner unter /tmp nicht aufgeräumt")

    def test_wrong_calls(self) -> None:
        cases = [
            [],                                                     # ohne <streaming-pc>
            ["--lokal"],                                            # --lokal ohne Testoptionen
            ["--lokal", "--ziel", str(self.target), "--ohne-einheit"],
            ["--ziel", str(self.target), "--datenordner", str(self.data)],  # mit Einheit
            ["127.0.0.1", "--lokal", "--ziel", str(self.target), "--datenordner", str(self.data),
             "--ohne-einheit"],
            ["--unbekannt"],
        ]
        for args in cases:
            with self.subTest(args=args):
                result = run(str(INSTALL), *args)
                self.assertEqual(result.returncode, 2, result.stderr)
        # --hier nur aus einem zusammengestellten Paket (version.txt), nie aus dem Repo.
        result = run(str(INSTALL), "--hier", "--ziel", str(self.target), "--datenordner",
                     str(self.data), "--ohne-einheit")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(self.target.exists())


class SystemSetupTest(unittest.TestCase):
    def test_syntax(self) -> None:
        for script in (INSTALL, SYSTEM):
            with self.subTest(script=script.name):
                result = subprocess.run(["bash", "-n", str(script)], capture_output=True, text=True,
                                        timeout=SCRIPT_TIMEOUT_S)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_dry_run_add_and_remove(self) -> None:
        rule = ('rule family="ipv4" source address="127.0.0.1" port port="8093" protocol="tcp" '
                'accept')
        result = run(str(SYSTEM), "--trocken", "--zone", "testzone", "127.0.0.1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"firewall-cmd --permanent --zone=testzone --add-rich-rule='{rule}'",
                      result.stdout)
        self.assertIn("firewall-cmd --reload", result.stdout)
        self.assertIn("nichts ausgeführt", result.stdout)

        result = run(str(SYSTEM), "--entfernen", "--trocken", "--port", "8100", "127.0.0.1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--zone=<aktive-zone> --remove-rich-rule='"
                      + rule.replace("8093", "8100") + "'", result.stdout)

    def test_dry_run_zone_from_route(self) -> None:
        # Wie am Streaming-PC (P15 b6): zwei aktive Zonen, die Kabelkarte in der einen, das
        # VPN-Gerät in «trusted». Ersatz für ip und firewall-cmd; gewählt wird die Zone der
        # Schnittstelle aus `ip route get`, --zone hat Vorrang, ohne Zone bleibt der Platzhalter.
        bin_dir = FOLDER / "zonen-bin"
        bin_dir.mkdir(exist_ok=True)
        stubs = {
            "ip": 'echo "$3 via 192.0.2.1 dev kabel0 src 192.0.2.5 uid 1000"\necho "    cache"\n',
            "firewall-cmd": ('case "$1" in\n'
                             '  --get-zone-of-interface=kabel0) echo "${KABEL_ZONE:-heimzone}" ;;\n'
                             '  --get-active-zones) printf "heimzone\\n  interfaces: kabel0\\n'
                             'trusted\\n  interfaces: vpn0\\n" ;;\n'
                             '  *) echo "no zone"; exit 2 ;;\nesac\n'),
        }
        for name, body in stubs.items():
            (bin_dir / name).write_text("#!/usr/bin/env bash\n" + body, encoding="utf-8")
            (bin_dir / name).chmod(0o755)
        path = {"PATH": f"{bin_dir}:{os.environ.get('PATH', '')}"}

        result = run(str(SYSTEM), "--trocken", "127.0.0.1", extra_env=path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--zone=heimzone --add-rich-rule=", result.stdout)
        self.assertIn("Schnittstelle kabel0", result.stdout)

        result = run(str(SYSTEM), "--trocken", "--zone", "andere", "127.0.0.1", extra_env=path)
        self.assertIn("--zone=andere --add-rich-rule=", result.stdout)

        result = run(str(SYSTEM), "--trocken", "127.0.0.1",
                     extra_env={**path, "KABEL_ZONE": "no zone"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--zone=<aktive-zone> --add-rich-rule=", result.stdout)

    def test_wrong_calls(self) -> None:
        cases = [
            ["--trocken"],                                   # ohne <studio-pc>
            ["--trocken", "192.0.2.300"],
            ["--trocken", "192.0.2.0/24"],
            ["--trocken", "studio"],
            ["--trocken", "01.2.3.4"],
            ["--trocken", "--port", "80", "127.0.0.1"],
            ["--trocken", "--port", "40000", "127.0.0.1"],
            ["--trocken", "--zone", "a b", "127.0.0.1"],
            ["--trocken", "127.0.0.1", "127.0.0.2"],
        ]
        for args in cases:
            with self.subTest(args=args):
                result = run(str(SYSTEM), *args)
                self.assertEqual(result.returncode, 2, result.stderr)

    @unittest.skipIf(os.geteuid() == 0, "läuft als root")
    def test_without_sudo_does_nothing(self) -> None:
        result = run(str(SYSTEM), "127.0.0.1")
        self.assertEqual(result.returncode, 1)
        self.assertIn("braucht sudo", result.stderr)
        self.assertNotIn("firewall-cmd --permanent", result.stdout)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Aufruf: test_installieren.py <testordner>")
    FOLDER = Path(sys.argv.pop(1)).resolve()
    FOLDER.mkdir(parents=True, exist_ok=True)
    unittest.main(verbosity=2)
