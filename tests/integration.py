"""Exercise the C server through the same HTTP routes used by the dashboard."""
import http.client
import json
import socket
import subprocess
import tempfile
import time
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(port, method, path, fields=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    body = urllib.parse.urlencode(fields) if fields is not None else None
    headers = {"Content-Type": "application/x-www-form-urlencoded"} if body is not None else {}
    connection.request(method, path, body=body, headers=headers)
    response = connection.getresponse()
    payload = response.read()
    status = response.status
    connection.close()
    return status, json.loads(payload)


def start(port, data):
    process = subprocess.Popen(
        [str(ROOT / "admission"), "--port", str(port), "--data", str(data)],
        cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError(f"C server stopped: {process.stderr.read().decode()}")
        try:
            request(port, "GET", "/api/state")
            return process
        except (OSError, TimeoutError):
            time.sleep(0.02)
    process.terminate()
    raise RuntimeError("C server did not start")


def applicant(state, name):
    return next(a for a in state["applicants"] if a["name"] == name)


def console_fallback():
    """Still validate heap allocation and saved state without socket privileges."""
    with tempfile.TemporaryDirectory() as directory:
        data = Path(directory) / "admissions.tsv"
        commands = """5
0
1
1
Earlier tie
95
85
90
0
1
Lower board
95
70
100
0
1
Later tie
95
85
90
0
2
0
3
1001
4
0
"""
        first = subprocess.run([str(ROOT / "admission"), "--console", "--data", str(data)],
                               cwd=ROOT, input=commands, text=True, capture_output=True, check=True)
        assert "Application ID: 1001" in first.stdout
        assert "Allocated 1 seat(s)." in first.stdout
        assert "next waiting applicant promoted" in first.stdout
        assert "Lower board" in first.stdout
        fields = data.read_text().splitlines()
        assert fields[0].startswith("ADMISSIONS2 ")
        assert fields[1].split("\t")[6] == "2"  # Earlier tie cancelled.
        assert fields[2].split("\t")[6] == "0"  # Lower board waits.
        assert fields[3].split("\t")[6] == "1"  # Later tie promoted.
        second = subprocess.run([str(ROOT / "admission"), "--console", "--data", str(data)],
                                cwd=ROOT, input="4\n0\n", text=True, capture_output=True, check=True)
        assert "1003" in second.stdout and "Admitted" in second.stdout
        old = Path(directory) / "old.tsv"
        old.write_text("ADMISSIONS1 1002 2 3 2 2 2\n1001\t1\t88\t90\t0\t0\tLegacy Student\n")
        migrated = subprocess.run([str(ROOT / "admission"), "--console", "--data", str(old)],
                                  cwd=ROOT, input="4\n5\n4\n3\n0\n", text=True, capture_output=True, check=True)
        assert "Legacy Student" in migrated.stdout and "Information Technology" in migrated.stdout
        assert old.read_text().startswith("ADMISSIONS2 ")
        assert "\t-1\t0\t0\tLegacy Student" in old.read_text()
    print("PASS: C heap allocation, tie breaks, promotion, persistence, old data migration (console mode)")


def main():
    try:
        probe = free_port()
    except PermissionError:
        console_fallback()
        print("HTTP integration test skipped: this environment blocks local sockets.")
        return
    with tempfile.TemporaryDirectory() as directory:
        data = Path(directory) / "admissions.tsv"
        port = probe
        process = start(port, data)
        try:
            assert all(dep["capacity"] == 0 for dep in request(port, "GET", "/api/state")[1]["departments"])
            assert request(port, "POST", "/api/capacity", {"department": 0, "capacity": 1})[0] == 200
            for name, score, board, tenth in [
                ("Earlier tie", 95, 85, 90), ("Lower tenth", 95, 85, 80),
                ("Later tie", 95, 85, 90), ("Lower board", 95, 70, 100),
                ("Lower entrance", 90, 100, 100),
            ]:
                assert request(port, "POST", "/api/applicants", {
                    "name": name, "department": 0, "score": score, "board": board, "tenth": tenth,
                })[0] == 200
            status, state = request(port, "GET", "/api/state")
            assert len(state["departments"]) == 8
            assert status == 200 and state["departments"][0]["heap"][0] == applicant(state, "Earlier tie")["id"]
            assert state["departments"][0]["ranking"] == [
                applicant(state, name)["id"] for name in
                ("Earlier tie", "Later tie", "Lower tenth", "Lower board", "Lower entrance")
            ]
            assert request(port, "POST", "/api/allocate", {"department": 0})[0] == 200
            state = request(port, "GET", "/api/state")[1]
            assert applicant(state, "Earlier tie")["status"] == "Admitted"
            assert state["departments"][0]["heap"][0] == applicant(state, "Later tie")["id"]
            assert request(port, "POST", "/api/cancel", {"id": applicant(state, "Earlier tie")["id"]})[0] == 200
            state = request(port, "GET", "/api/state")[1]
            assert applicant(state, "Later tie")["status"] == "Admitted"
            assert state["departments"][0]["heap"][0] == applicant(state, "Lower tenth")["id"]
            assert request(port, "POST", "/api/capacity", {"department": 0, "capacity": 0})[0] == 400
            assert request(port, "POST", "/api/applicants", {
                "name": "Invalid", "department": 0, "score": 101, "board": 80, "tenth": 80,
            })[0] == 400
            assert request(port, "POST", "/api/applicants", {
                "name": "New branch", "department": 6, "score": 80, "board": 80, "tenth": 80,
            })[0] == 200
        finally:
            process.terminate()
            process.wait(timeout=3)

        # Restart: records and the waiting heap must be reconstructed.
        process = start(port, data)
        try:
            state = request(port, "GET", "/api/state")[1]
            assert applicant(state, "Earlier tie")["status"] == "Cancelled"
            assert applicant(state, "Later tie")["status"] == "Admitted"
            assert state["departments"][0]["heap"][0] == applicant(state, "Lower tenth")["id"]
            assert request(port, "POST", "/api/cancel", {"id": applicant(state, "Later tie")["id"]})[0] == 200
            state = request(port, "GET", "/api/state")[1]
            assert applicant(state, "Lower tenth")["status"] == "Admitted"
            assert applicant(state, "New branch")["department"] == 6
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
            connection.request("GET", "/")
            response = connection.getresponse()
            assert response.status == 200 and b"MeritSeat" in response.read()
            connection.close()
        finally:
            process.terminate()
            process.wait(timeout=3)
    print("PASS: priority, tie breaks, allocation, promotion, validation, persistence, UI")


if __name__ == "__main__":
    main()
