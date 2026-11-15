// Minimal MQTT 3.1.1 client (QoS 0 only): connect with an optional retained Last Will,
// publish, subscribe, keep-alive pings, and a callback for incoming PUBLISH packets.
// No dependencies, C# 7.3 / .NET Framework 4.7.2 compatible.

using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace EhsLink
{
    internal sealed class MqttClient : IDisposable
    {
        private readonly object _writeLock = new object();
        private TcpClient _tcp;
        private NetworkStream _stream;
        private Thread _reader;
        private Timer _pinger;
        private volatile bool _open;
        private ushort _packetId;

        public string Host { get; private set; }
        public bool IsConnected => _open;

        /// <summary>Raised on the reader thread for every incoming PUBLISH (topic, payload).</summary>
        public event Action<string, byte[]> MessageReceived;
        /// <summary>Raised once when an open connection drops.</summary>
        public event Action Disconnected;

        public void Connect(string host, int port, string clientId, int keepAliveSec,
                            string willTopic = null, byte[] willPayload = null, bool willRetain = false)
        {
            Close();
            var tcp = new TcpClient { NoDelay = true };
            var ar = tcp.BeginConnect(host, port, null, null);
            if (!ar.AsyncWaitHandle.WaitOne(5000)) { tcp.Close(); throw new IOException("Connect timeout to " + host); }
            tcp.EndConnect(ar);
            var stream = tcp.GetStream();
            stream.ReadTimeout = 10000;

            // CONNECT
            var body = new MemoryStream();
            WriteString(body, "MQTT");
            body.WriteByte(4);                                   // protocol level 3.1.1
            byte flags = 0x02;                                   // clean session
            if (willTopic != null) { flags |= 0x04; if (willRetain) flags |= 0x20; }
            body.WriteByte(flags);
            body.WriteByte((byte)(keepAliveSec >> 8)); body.WriteByte((byte)keepAliveSec);
            WriteString(body, clientId);
            if (willTopic != null) { WriteString(body, willTopic); WriteBytes(body, willPayload ?? new byte[0]); }
            WritePacket(stream, 0x10, body.ToArray());

            // CONNACK
            int type = stream.ReadByte();
            int len = stream.ReadByte();
            var ack = ReadExact(stream, len);
            if (type != 0x20 || len != 2 || ack[1] != 0) { tcp.Close(); throw new IOException("Broker refused connection (code " + (len == 2 ? ack[1] : -1) + ")"); }

            stream.ReadTimeout = Timeout.Infinite;
            _tcp = tcp; _stream = stream; Host = host; _open = true;
            _reader = new Thread(ReadLoop) { IsBackground = true, Name = "EHS Link MQTT" };
            _reader.Start();
            int pingMs = Math.Max(5, keepAliveSec / 2) * 1000;
            _pinger = new Timer(_ => { try { Send(0xC0, new byte[0]); } catch { Drop(); } }, null, pingMs, pingMs);
        }

        public void Publish(string topic, byte[] payload, bool retain)
        {
            var body = new MemoryStream();
            WriteString(body, topic);
            body.Write(payload, 0, payload.Length);
            Send((byte)(0x30 | (retain ? 1 : 0)), body.ToArray());
        }

        public void Subscribe(string topicFilter)
        {
            var body = new MemoryStream();
            ushort id = ++_packetId == 0 ? ++_packetId : _packetId;
            body.WriteByte((byte)(id >> 8)); body.WriteByte((byte)id);
            WriteString(body, topicFilter);
            body.WriteByte(0);                                   // requested QoS 0
            Send(0x82, body.ToArray());
        }

        /// <summary>Clean disconnect (no Last Will is sent by the broker).</summary>
        public void Disconnect()
        {
            if (!_open) return;
            try { Send(0xE0, new byte[0]); } catch { }
            Close();
        }

        public void Dispose() { Disconnect(); }

        // ------------------------------------------------------------------ internals

        private void Send(byte header, byte[] body)
        {
            lock (_writeLock)
            {
                if (_stream == null) throw new IOException("Not connected");
                WritePacket(_stream, header, body);
            }
        }

        private void ReadLoop()
        {
            var stream = _stream;
            try
            {
                while (_open)
                {
                    int header = stream.ReadByte();
                    if (header < 0) break;
                    int len = 0, mult = 1, b;
                    do { b = stream.ReadByte(); if (b < 0) throw new IOException("eof"); len += (b & 0x7F) * mult; mult *= 128; } while ((b & 0x80) != 0);
                    var data = ReadExact(stream, len);
                    if ((header >> 4) == 3)                      // PUBLISH
                    {
                        int tlen = (data[0] << 8) | data[1];
                        string topic = Encoding.UTF8.GetString(data, 2, tlen);
                        int pos = 2 + tlen;
                        if (((header >> 1) & 3) > 0) pos += 2;   // packet id (QoS>0; we never ask for it)
                        var payload = new byte[len - pos];
                        Buffer.BlockCopy(data, pos, payload, 0, payload.Length);
                        try { MessageReceived?.Invoke(topic, payload); } catch { }
                    }
                }
            }
            catch { }
            Drop();
        }

        private void Drop()
        {
            if (!_open) return;
            Close();
            try { Disconnected?.Invoke(); } catch { }
        }

        private void Close()
        {
            _open = false;
            try { _pinger?.Dispose(); } catch { }
            try { _tcp?.Close(); } catch { }
            _pinger = null; _tcp = null; _stream = null;
        }

        private static void WritePacket(Stream s, byte header, byte[] body)
        {
            var buf = new MemoryStream(body.Length + 5);
            buf.WriteByte(header);
            int len = body.Length;
            do { byte d = (byte)(len % 128); len /= 128; if (len > 0) d |= 0x80; buf.WriteByte(d); } while (len > 0);
            buf.Write(body, 0, body.Length);
            var bytes = buf.ToArray();
            s.Write(bytes, 0, bytes.Length);
            s.Flush();
        }

        private static void WriteString(Stream s, string str) { WriteBytes(s, Encoding.UTF8.GetBytes(str)); }

        private static void WriteBytes(Stream s, byte[] b)
        {
            s.WriteByte((byte)(b.Length >> 8)); s.WriteByte((byte)b.Length);
            s.Write(b, 0, b.Length);
        }

        private static byte[] ReadExact(Stream s, int n)
        {
            var buf = new byte[n];
            int got = 0;
            while (got < n)
            {
                int r = s.Read(buf, got, n - got);
                if (r <= 0) throw new IOException("eof");
                got += r;
            }
            return buf;
        }
    }
}
