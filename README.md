# TaskMini

A lightweight, fast, and feature-rich process monitor for macOS built with GTK+3. TaskMini provides real-time system monitoring with an intuitive interface similar to Activity Monitor but with enhanced functionality.

## Features

### 🖥️ **System Monitoring**
- **Real-time process monitoring** with 0.5-second updates
- **CPU usage tracking** (normalized per core for accurate system-wide view)
- **Memory usage** with human-readable formatting (GB/MB/KB)
- **Network activity** per process with real-time transfer rates
- **GPU usage detection** with intelligent fallback when powermetrics requires root
- **Process runtime tracking** showing elapsed execution time

### 🛡️ **Process Management** 
- **Process type identification**: System processes marked with 🛡️ shield icon
- **Safe process termination**: Right-click context menu for user processes
- **System process protection**: Prevents accidental termination of critical system processes
- **Confirmation dialogs** with feedback on termination success/failure

### 📊 **System Information**
- **Comprehensive hardware specs**: CPU, memory, storage, graphics details
- **Dynamic system activity**: Network throughput, disk I/O, virtual memory stats
- **Real-time GPU status**: Idle/Light/Active/Busy/Heavy usage indicators
- **Hardware identification**: Model, serial number, macOS version

### 🎛️ **User Interface**
- **Sortable columns**: Click any column header to sort ascending/descending
- **Scroll position preservation**: Maintains view position during updates
- **Responsive design**: Clean, modern GTK+3 interface
- **Background threading**: Non-blocking UI with threaded data collection

## Screenshots

*TaskMini showing real-time process monitoring with system information*

## Installation

### Prerequisites

Make sure you have the required dependencies installed:

```bash
# Install GTK+3 development libraries (if not already installed)
brew install gtk+3 pkg-config
```

### Building from Source

1. **Clone the repository**:
   ```bash
   git clone https://github.com/yourusername/TaskMini.git
   cd TaskMini
   ```

2. **Compile the application**:
   ```bash
   gcc `pkg-config --cflags gtk+-3.0` -o TaskMini TaskMini.c `pkg-config --libs gtk+-3.0` -lpthread
   ```

3. **Run TaskMini**:
   ```bash
   ./TaskMini
   ```

### Alternative Build Methods

#### Using Make (optional)
```bash
make
make install  # Optional: install to /usr/local/bin
```

#### Quick Build Script
```bash
chmod +x build.sh
./build.sh
```

## Usage

### Basic Operation
- **Launch**: Run `./TaskMini` or double-click the executable
- **Sort processes**: Click any column header to sort by that metric
- **Terminate processes**: Right-click on user processes to safely terminate them
- **System processes**: Protected system processes show a 🛡️ shield icon

### Keyboard Shortcuts
- **Refresh**: Automatic every 0.5 seconds
- **Quit**: Cmd+Q or close window

### Understanding the Interface

| Column | Description |
|--------|-------------|
| **PID** | Process identifier |
| **Name** | Process/application name |
| **CPU** | CPU usage (% of total system capacity) |
| **GPU** | Graphics processing usage or activity level |
| **Memory** | RAM usage in human-readable format |
| **Network** | Network transfer rate (KB/s) |
| **Run Time** | How long the process has been running |
| **Type** | 🛡️ System or User process classification |

## System Requirements

- **Operating System**: macOS 10.12 (Sierra) or later
- **Dependencies**: GTK+3, GLib
- **Hardware**: Any Mac with Apple Silicon or Intel processor
- **Privileges**: No root/admin privileges required

## Technical Details

### Architecture
- **Single-file implementation**: All functionality in one C source file
- **Multi-threaded design**: Background data collection with main thread UI updates
- **Memory-safe**: Proper allocation/deallocation with leak prevention
- **Performance optimized**: Efficient system calls and minimal overhead

### GPU Detection
TaskMini uses a smart fallback system for GPU monitoring:

1. **Primary method**: `powermetrics` (if available with permissions)
2. **Fallback methods**: 
   - WindowServer CPU usage analysis
   - Graphics-intensive process detection
   - System activity correlation
   - Qualitative status indicators

### Network Monitoring
- Per-process network tracking using `nettop`
- Rate calculation with time-based differentials
- Cumulative system-wide statistics
- Human-readable transfer rates

## Contributing

Contributions are welcome! Here's how you can help:

1. **Fork the repository**
2. **Create a feature branch**: `git checkout -b feature-name`
3. **Make your changes** and test thoroughly
4. **Commit your changes**: `git commit -am 'Add feature description'`
5. **Push to the branch**: `git push origin feature-name`
6. **Submit a Pull Request**

### Development Guidelines
- Follow existing code style and formatting
- Test on multiple macOS versions if possible
- Ensure memory safety and proper resource cleanup
- Update documentation for new features

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- **GTK+ Development Team** for the excellent GUI toolkit
- **Apple Developer Documentation** for system monitoring APIs
- **macOS Community** for insights into system process management

## Troubleshooting

### Common Issues

**Q: "powermetrics must be invoked as the superuser" error**
A: This is normal and expected. TaskMini automatically switches to fallback GPU detection methods that don't require root privileges.

**Q: Network rates show as 0.0 KB/s**
A: Network monitoring requires a few update cycles to calculate accurate rates. Wait 1-2 seconds after launch.

**Q: Some system processes don't show network usage**
A: This is normal - many system processes don't generate significant network traffic.

**Q: Application crashes on older macOS versions**
A: Ensure you have GTK+3 properly installed via Homebrew and try rebuilding.

### Support
For bugs, feature requests, or questions:
- **Create an issue** on GitHub
- **Check existing issues** for solutions
- **Provide system info** (macOS version, hardware) when reporting bugs

---

**TaskMini** - Simple, powerful, and safe process monitoring for macOS.
