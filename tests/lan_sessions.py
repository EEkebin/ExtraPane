"""No driver/capture: concurrent automatic TLS, capacity and isolated failures.

The unified host stops serving when stdin reaches EOF (launcher control), so
keep a pipe open; an inherited closed/null stdin made it exit right after LISTENING."""
import socket, ssl, subprocess, sys, threading, time

exe=sys.argv[1]
with socket.socket() as probe:
    probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
ctx.check_hostname=False;ctx.verify_mode=ssl.CERT_NONE
ctx.minimum_version=ctx.maximum_version=ssl.TLSVersion.TLSv1_3
host=subprocess.Popen([exe,'host','--lan','--bind','127.0.0.1','--port',str(port),'--allow-screen','--extend','--serve'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
logs=[];clients=[]
def reader():
    for line in host.stdout:logs.append(line.strip())
threading.Thread(target=reader,daemon=True).start()
def wait(condition):
    until=time.monotonic()+10
    while time.monotonic()<until:
        if condition():return
        time.sleep(.02)
    raise AssertionError(logs)
def connect():
    stream=ctx.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=3))
    clients.append(stream);return stream
try:
    wait(lambda:any(line.startswith('LISTENING') for line in logs))
    for _ in range(4):connect()
    try:
        extra=connect();raise AssertionError('Fifth TLS session was accepted')
    except (ssl.SSLError,ConnectionError,OSError):pass
    # A malformed request only closes its own connection. No display is made.
    clients[0].sendall(bytes(32));clients[0].close()
    wait(lambda:any('CLIENT_DISCONNECTED slot=1' in line for line in logs))
    replacement=connect();assert replacement.version()=='TLSv1.3'
    for stream in clients[1:]:
        if stream.fileno()!=-1:stream.sendall(bytes(32))
    wait(lambda:sum(line.startswith('CLIENT_DISCONNECTED') for line in logs)>=5)
    assert host.poll() is None
    assert not any(line.startswith('CAPTURE_SOURCE') for line in logs)
    print('Automatic TLS: four clients, capacity rejection, isolated malformed requests and reconnect passed')
finally:
    for stream in clients:stream.close()
    if host.poll() is None:host.terminate()
    host.wait(timeout=5)
