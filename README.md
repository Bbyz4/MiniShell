# MiniShell

This project is a custom shell implementation developed as part of the **Operating Systems** course at Jagiellonian University Kraków. The shell was implemented incrementally through multiple stages, adding new functionalities at each stage.  

The main goal was to explore different operating systems concepts and apply the theoretical knowledge gained during the course in a practical environment

## Main Features

- **Basic shell functionality (Stage 1 & 2)**  
  - Reads input from the terminal or a file.  
  - Parses and executes commands.
  - Handles basic errors such as file not found, permission denied, or execution errors.
  - Handles errors for incorrect arguments or failed syscalls.  
  - Ensures proper cleanup after execution.
  - Supports multi-line input and partial reads when reading from a file.  

- **Built-in commands (Stage 3)**  
  - `exit` – exits the shell.  
  - `lcd [path]` – changes the current working directory (defaults to `HOME`).  
  - `lls` – lists files in the current directory (ignores hidden files).  
  - `lkill [ -signal_number ] pid` – sends a signal to a process or process group.  
  - Possibility to easily add new commands to the shell.

- **I/O redirection and pipelines (Stage 4)**  
  - Supports input (`<`) and output (`>`/`>>`) redirection for commands.  
  - Handles multiple commands connected with pipes (`|`).  
  - Executes multiple pipelines sequentially in one line.  

- **Background execution and signal handling (Stage 5)**  
  - Supports background execution using `&`.  
  - Monitors and reports termination of background processes.  
  - Avoids zombie processes.  
  - Handles `CTRL-C` (`SIGINT`) for foreground processes while leaving background processes unaffected.
 
---

This repository only contains shell's source files, performance tests and documentation are private.

## 🚀 Installation & Setup

### Clone the Repository
```bash
git clone https://github.com/Bbyz4/MiniShell.git
cd MiniShell/shell
```

### Build the Project
```bash
make
```

### To enter commands interactively, run the following:
```bash
./bin/mshell
```

### To execute commands from a premade file, run the following:
```bash
./bin/mshell < some_file.txt
```

### To clean build artifacts:
```bash
make clean       # removes compiled objects and executable
make full_clean  # removes everything including parser library
```
