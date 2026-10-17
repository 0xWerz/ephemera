// Workspace build adapter. Replaces only EpicGames.Core.SingleInstanceMutex
// in the separate build-tool copy used by this project's launcher.
using System;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace EpicGames.Core
{
    /// <summary>Cross-process build lock stored on the external workspace.</summary>
    public class SingleInstanceMutex : IDisposable
    {
        private SafeFileHandle? _file;

        // .NET's fixed-arity wrapper handles open's variadic ABI on Apple Silicon.
        [DllImport("System.Native", EntryPoint = "SystemNative_Open", SetLastError = true)]
        private static extern IntPtr OpenFile(string path, int flags, int mode);

        [DllImport("/usr/lib/libSystem.B.dylib", SetLastError = true)]
        private static extern int flock(int descriptor, int operation);

        private bool AcquireFile(string name, bool wait, CancellationToken cancellationToken)
        {
            string root = Environment.GetEnvironmentVariable("EPHEMERA_MUTEX_DIR")
                ?? throw new InvalidOperationException("External build locks require EPHEMERA_MUTEX_DIR.");
            if (!OperatingSystem.IsMacOS() || !Path.IsPathFullyQualified(root))
                throw new InvalidOperationException("External build lock adapter requires an absolute Mac path.");
            Directory.CreateDirectory(root);
            string filename = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(name))) + ".lock";
            // FileStream takes an implicit shared flock on macOS, which would
            // reject a competing opener before we can wait for our own lock.
            // System.Native flags: O_CREAT | O_RDWR | O_CLOEXEC; mode 0600.
            int descriptor = OpenFile(Path.Combine(root, filename), 0x0020 | 0x0002 | 0x0010, 0x180).ToInt32();
            if (descriptor < 0)
                throw new IOException("Cannot open external build lock; errno=" + Marshal.GetLastPInvokeError());
            _file = new SafeFileHandle((IntPtr)descriptor, ownsHandle: true);
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (flock(descriptor, 2 | 4) == 0) // LOCK_EX | LOCK_NB
                    return true;
                int error = Marshal.GetLastPInvokeError();
                if (error != 35 && error != 4) // EWOULDBLOCK, EINTR on macOS
                    throw new IOException("Cannot acquire external build lock; errno=" + error);
                if (!wait)
                    return false;
                cancellationToken.WaitHandle.WaitOne(50);
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (disposing)
            {
                // Closing the descriptor releases flock, including after a crash.
                // Keep the file so new processes always lock the same inode.
                _file?.Dispose();
                _file = null;
            }
        }

        public static IDisposable Acquire(string name) => AcquireAsync(name).GetAwaiter().GetResult();

        public static async Task<IDisposable> AcquireAsync(string name, CancellationToken cancellationToken = default)
        {
            SingleInstanceMutex mutex = new();
            try
            {
                await Task.Run(() => mutex.AcquireFile(name, true, cancellationToken), cancellationToken).ConfigureAwait(false);
                return mutex;
            }
            catch
            {
                mutex.Dispose();
                throw;
            }
        }

        public static bool TryAcquire(string name, [NotNullWhen(true)] out IDisposable? mutex)
        {
            SingleInstanceMutex candidate = new();
            try
            {
                if (candidate.AcquireFile(name, false, default))
                {
                    mutex = candidate;
                    return true;
                }
            }
            catch
            {
                candidate.Dispose();
                throw;
            }
            candidate.Dispose();
            mutex = null;
            return false;
        }
    }
}
