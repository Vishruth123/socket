import socket
import select
import struct
import sys

SERVER_IP = "172.18.128.3" #######

SERVER_PORT = 5000
MAXNODES = 26

def recv_exact(sock,l):
    data = bytearray()
    while len(data) < l:
        chunk = sock.recv(l - len(data))
        if not chunk:
            return None
        data.extend(chunk)
    return bytes(data)

def send_all(sock,data):
    try:
        sock.sendall(data)
        return True
    except OSError:
        return False

def read_config(filename):
    rows = []
    with open(filename,"r") as fin:
        for line_number, line in enumerate(fin,start=1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                row = [int(value) for value in line.split()]
            except ValueError:
                raise ValueError(f"Invalid int on line {line_number}")
            if not row:
                raise ValueError(f"Empty row on line {line_number}")
            rows.append(row)

    N = len(rows) + 1
    if N > MAXNODES:
        raise ValueError("Maximum nodes is only 26")
    costs = [[-1]*N for _ in range(N)]
    for i in range(N):
        costs[i][i] = 0
    for i, row in enumerate(rows):
        expected = N-i-1
        if len(row) != expected:
            raise ValueError(f"Row {i+1}: expected {expected} values, "f"got {len(row)}")
        for j, cost in enumerate(row,start = i+1):
            if cost != -1 and cost <= 0:
                raise ValueError(f"Invalid link cost {cost} in row {i+1}")
            costs[i][j] = cost
            costs[j][i] = cost
    return costs

def maketuple(node,cost):
    return (node["id"].encode("ascii") + node["ip"] + struct.pack("!HI",node["port"], cost))

def send_linkstate(nodeid,nodes,costs):
    N = len(costs)
    body = bytearray()
    tuples = []
    if nodeid >= N:
        tuples.append(maketuple(nodes[nodeid],0))
    else:
        for k in range(N):
            if k == nodeid:
                tuples.append(maketuple(nodes[k],0))
            elif costs[nodeid][k] != -1:
                tuples.append(maketuple(nodes[k],costs[nodeid][k]))
    body.append(len(tuples))
    for item in tuples:
        body.extend(item)
    sock = nodes[nodeid]["sock"]
    return sock is not None and send_all(sock,body)

def recieve_connect(sock):
    body = recv_exact(sock,6)
    if body is None:
        return None
    ip = body[:4]
    port = struct.unpack("!H",body[4:6])[0]
    # print("UDP PORT = ",port)
    return ip,port

def close_node(node):
    sock = node["sock"]
    if sock is not None:
        try:
            sock.close()
        except OSError:
            pass
        node["sock"] = None

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 server.py <configfile>")
        return 1
    configfile = sys.argv[1]
    try:
        costs = read_config(configfile)
    except (OSError,ValueError) as exc:
        print(f"Configuration error: {exc}", file=sys.stderr)
        return 1
    N=len(costs)
    server = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    try:
        server.bind((SERVER_IP,SERVER_PORT))
        server.listen(27)
    except OSError as exc:
        print(f"Server setup failed: {exc}", file=sys.stderr)
        server.close()
        return 1
    print(f"ON listening on {SERVER_IP}:{SERVER_PORT}")
    nodes = []
    try:
        while len(nodes) < N:
            client, address = server.accept()
            try:
                connect_data = recieve_connect(client)
                if connect_data is None:
                    print("VN disconnected before completing CONNECT")
                    client.close()
                    continue
                ip,port = connect_data
                node_id = len(nodes)
                nodes.append({
                    "id": chr(ord("A")+node_id),
                    "sock": client,
                    "ip": ip,
                    "port": port,
                })
                print(f"Connected VN {nodes[-1]['id']} from {address}")
            except OSError as exc:
                print(f"CONNECT failed: {exc}", file=sys.stderr)
                client.close()
        for j in range(N):
            if not send_linkstate(j,nodes,costs):
                print(f"Failed to send initial LINKSTATE to {nodes[j]['id']}",file=sys.stderr)
                close_node(nodes[j])
        print("Initial topology sent.")
        while True:
            active_sockets = [node["sock"] for node in nodes if node["sock"] is not None]
            readable = [server] + active_sockets
            try:
                ready,_,_ = select.select(readable,[],[],1.0)
            except OSError as exc:
                print(f"select failed: {exc}", file=sys.stderr)
                break
            if server in ready:
                try:
                    client, address = server.accept()
                    if len(nodes) >= MAXNODES:
                        client.close()
                    else:
                        connect_data = recieve_connect(client)
                        if connect_data is None:
                            client.close()
                        else:
                            ip, port = connect_data
                            node_id = len(nodes)

                            nodes.append({
                                "id": chr(ord("A") + node_id),
                                "sock": client,
                                "ip": ip,
                                "port": port,
                            })
                            if node_id >= N:
                                if not send_linkstate(
                                    node_id, nodes, costs
                                ):
                                    close_node(nodes[node_id])
                except OSError as exc:
                    print(f"Accept/CONNECT error: {exc}", file=sys.stderr)

            for node in nodes:
                sock = node["sock"]
                if sock is None or sock not in ready:
                    continue
                try:
                    data = sock.recv(1024)
                    if not data:
                        print(f"VN {node['id']} disconnected")
                        close_node(node)
                    else:
                        print(f"Unexpected data from VN {node['id']}: {len(data)} bytes")
                except OSError as exc:
                    print(f"Receive failed for VN {node['id']}: {exc}", file=sys.stderr)
                    close_node(node)
            try:
                new_costs = read_config(configfile)
            except (OSError, ValueError) as exc:
                print(f"Config reload failed so keeping old topology: {exc}", file=sys.stderr)
                continue
            if len(new_costs) != N or new_costs == costs:
                continue
            costs = new_costs
            print("topology changed, sending updated LINKSTATES")
            for j, node in enumerate(nodes):
                if node["sock"] is None:
                    continue
                if not send_linkstate(j,nodes,costs):
                    print(f"Update failed for VN {node['id']}", file=sys.stderr)
                    close_node(node)
    except KeyboardInterrupt:
        print("ON shutting down.")
    finally:
        for node in nodes:
            close_node(node)
        server.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())