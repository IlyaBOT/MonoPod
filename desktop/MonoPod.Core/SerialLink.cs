using System.IO.Ports;

namespace MonoPod.Core;
// All synchronous SerialPort calls run on a worker; bounded ReadTimeout makes unplug/cancel finite.
public interface ISerialLink : IDisposable
{
    int Read(byte[] buffer, int offset, int count);
    void Write(byte[] buffer, int offset, int count);
}

public sealed class SerialLink : ISerialLink
{
    readonly SerialPort port;
    public SerialLink(string name)
    {
        port = new(name, 115200)
        {
            ReadTimeout = 300,
            WriteTimeout = 8000,
            DtrEnable = true,
            RtsEnable = false,
            ReadBufferSize = 16384,
            WriteBufferSize = 4096
        };
        try
        {
            port.Open();
            // Match the firmware's native-CDC probe: allow DTR/CDC startup to settle.
            // This runs on the transport worker, never on the WinForms UI thread.
            Thread.Sleep(500);
            // An aborted receive can leave ERR TIMEOUT_OR_DISCONNECT queued.
            port.DiscardInBuffer();
        }
        catch
        {
            port.Dispose();
            throw;
        }
    }

    public int Read(byte[] b, int o, int n) => port.Read(b, o, n);
    public void Write(byte[] b, int o, int n) => UsbPacketWriter.Write(b, o, n, port.Write, Thread.Sleep);
    public void Dispose() => port.Dispose();
}
