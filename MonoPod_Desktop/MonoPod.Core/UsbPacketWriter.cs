namespace MonoPod.Core;

// Old MPOD1 v4 firmware requests 1024 bytes but HWCDC defaults to a 256-byte
// device RX queue. Host SerialPort buffer sizes cannot enlarge that queue.
// Preserve the requested logical block; pace its 64-byte USB packets instead.
public static class UsbPacketWriter
{
    public const int PacketBytes = 64;
    public const int PauseMilliseconds = 8;

    public static void Write(byte[] buffer, int offset, int count,
        Action<byte[], int, int> write, Action<int> pause)
    {
        ArgumentNullException.ThrowIfNull(buffer);
        ArgumentOutOfRangeException.ThrowIfNegative(offset);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (offset > buffer.Length - count) throw new ArgumentException("Invalid USB buffer range.");
        while (count > 0)
        {
            int n = Math.Min(PacketBytes, count);
            write(buffer, offset, n);
            offset += n;
            count -= n;
            if (count > 0) pause(PauseMilliseconds);
        }
    }
}
