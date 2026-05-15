#ifndef DFTRACER_SERVER_COMMON_DATASTRUCTURE
#define DFTRACER_SERVER_COMMON_DATASTRUCTURE
namespace dftracer {
struct CpuMetrics {
  unsigned long long user = 0;
  unsigned long long nice = 0;
  unsigned long long system = 0;
  unsigned long long idle = 0;
  unsigned long long iowait = 0;
  unsigned long long irq = 0;
  unsigned long long softirq = 0;
  unsigned long long steal = 0;
  unsigned long long guest = 0;
  unsigned long long guest_nice = 0;
};

struct MemMetrics {
  // Explicitly declare all required metrics as members
  unsigned long long MemAvailable = 0;
  unsigned long long Buffers = 0;
  unsigned long long Cached = 0;
  unsigned long long SwapCached = 0;
  unsigned long long Active = 0;
  unsigned long long Inactive = 0;
  unsigned long long Active_anon = 0;
  unsigned long long Inactive_anon = 0;
  unsigned long long Active_file = 0;
  unsigned long long Inactive_file = 0;
  unsigned long long Unevictable = 0;
  unsigned long long Mlocked = 0;
  unsigned long long SwapTotal = 0;
  unsigned long long SwapFree = 0;
  unsigned long long Dirty = 0;
  unsigned long long Writeback = 0;
  unsigned long long AnonPages = 0;
  unsigned long long Mapped = 0;
  unsigned long long Shmem = 0;
  unsigned long long KReclaimable = 0;
  unsigned long long Slab = 0;
  unsigned long long SReclaimable = 0;
  unsigned long long SUnreclaim = 0;
  unsigned long long KernelStack = 0;
  unsigned long long PageTables = 0;
  unsigned long long NFS_Unstable = 0;
  unsigned long long Bounce = 0;
  unsigned long long WritebackTmp = 0;
  unsigned long long CommitLimit = 0;
  unsigned long long Committed_AS = 0;
  unsigned long long VmallocTotal = 0;
  unsigned long long VmallocUsed = 0;
  unsigned long long VmallocChunk = 0;
  unsigned long long Percpu = 0;
  unsigned long long HardwareCorrupted = 0;
  unsigned long long AnonHugePages = 0;
  unsigned long long ShmemHugePages = 0;
  unsigned long long ShmemPmdMapped = 0;
  unsigned long long FileHugePages = 0;
  unsigned long long FilePmdMapped = 0;
  unsigned long long HugePages_Total = 0;
  unsigned long long HugePages_Free = 0;
  unsigned long long HugePages_Rsvd = 0;
  unsigned long long HugePages_Surp = 0;
  unsigned long long Hugepagesize = 0;
  unsigned long long Hugetlb = 0;
  unsigned long long DirectMap4k = 0;
  unsigned long long DirectMap2M = 0;
  unsigned long long DirectMap1G = 0;
};

struct IOMetrics {
  unsigned long long read_count = 0;      // Number of reads
  unsigned long long read_bytes = 0;      // Bytes read
  unsigned long long read_time_ms = 0;    // Time spent reading (ms)
  unsigned long long write_count = 0;     // Number of writes
  unsigned long long write_bytes = 0;     // Bytes written
  unsigned long long write_time_ms = 0;   // Time spent writing (ms)
  unsigned long long io_in_progress = 0;  // I/O operations in progress
};

struct NetworkMetrics {
  unsigned long long rx_bytes = 0;    // Bytes received
  unsigned long long rx_packets = 0;  // Packets received
  unsigned long long rx_errors = 0;   // Receive errors
  unsigned long long rx_dropped = 0;  // Packets dropped on RX
  unsigned long long tx_bytes = 0;    // Bytes transmitted
  unsigned long long tx_packets = 0;  // Packets transmitted
  unsigned long long tx_errors = 0;   // Transmit errors
  unsigned long long tx_dropped = 0;  // Packets dropped on TX
  unsigned long long collisions = 0;  // Collision count
};
}  // namespace dftracer
#endif  // DFTRACER_SERVER_COMMON_DATASTRUCTURE