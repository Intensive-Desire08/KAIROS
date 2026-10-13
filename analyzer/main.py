import urllib.request
import urllib.error
import json
import sys

def run_nist_tests(port=8080, bytes_count=1024, mode="full"):
    url = f"http://localhost:{port}/test"
    
    payload = {
        "bytes": bytes_count,
        "mode": mode
    }
    
    data = json.dumps(payload).encode('utf-8')
    
    req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'}, method='POST')
    import time
    
    try:
        print(f"Connecting to {url} to run {mode} NIST tests on {bytes_count} bytes...", file=sys.stderr)
        start_time = time.time()
        with urllib.request.urlopen(req) as response:
            result = json.loads(response.read().decode('utf-8'))
            end_time = time.time()
            duration = end_time - start_time
            if duration > 0:
                speed = bytes_count / duration
                print(f"Time taken: {duration:.2f} s", file=sys.stderr)
                print(f"Speed: {speed:.2f} B/s", file=sys.stderr)
            print(json.dumps(result, indent=2))
    except urllib.error.URLError as e:
        print(f"Error connecting to backend API: {e}", file=sys.stderr)
        print("Make sure the C++ backend is running and the hardware is connected on COM3.", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"An error occurred: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    run_nist_tests(bytes_count=4096, mode="full")
