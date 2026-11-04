#!/usr/bin/env python3
"""
CMP2 File Compression Utility - Tkinter GUI
Automates compression, decompression, batch processing, and benchmark inspection
using the native C++ compressor and benchmark_cli binaries.
"""

import os
import sys
import subprocess
import threading
import json
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path


class CompressorGUI(tk.Tk):
    def __init__(self):
        super().__init__()

        self.title("CMP2 File Compression Utility")
        self.geometry("820x680")
        self.minsize(700, 560)

        # Base directories to search for executables
        self.project_root = Path(__file__).resolve().parent
        self.compressor_bin = self._find_binary("compressor")
        self.benchmark_bin = self._find_binary("benchmark_cli")

        self._configure_styles()
        self._build_ui()
        self._check_binaries()

    def _find_binary(self, name: str) -> Path | None:
        """Find executable in standard build output directories."""
        candidates = [
            self.project_root / "build" / name,
            self.project_root / "W3" / "build" / name,
            self.project_root / "build" / "Debug" / name,
            self.project_root / "build" / "Release" / name,
        ]
        for p in candidates:
            if p.is_file() and os.access(p, os.X_OK):
                return p
        return None

    def _configure_styles(self):
        self.style = ttk.Style(self)
        # Use available modern theme
        available_themes = self.style.theme_names()
        for t in ("aqua", "clam", "default"):
            if t in available_themes:
                self.style.theme_use(t)
                break

        self.style.configure("Header.TLabel", font=("Helvetica", 16, "bold"))
        self.style.configure("SubHeader.TLabel", font=("Helvetica", 11), foreground="#555")
        self.style.configure("Bold.TLabel", font=("Helvetica", 11, "bold"))
        self.style.configure("Action.TButton", font=("Helvetica", 12, "bold"), padding=6)
        self.style.configure("Status.TLabel", font=("Helvetica", 10))

    def _build_ui(self):
        # Header banner
        header_frame = ttk.Frame(self, padding=(16, 12, 16, 8))
        header_frame.pack(fill=tk.X)

        title_lbl = ttk.Label(header_frame, text="CMP2 Compression & Decompression Utility", style="Header.TLabel")
        title_lbl.pack(anchor=tk.W)

        sub_lbl = ttk.Label(header_frame, text="Fast block-wise LZ77 + Huffman + CRC-32 integrity pipeline", style="SubHeader.TLabel")
        sub_lbl.pack(anchor=tk.W)

        # Notebook tabs
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=16, pady=8)

        # Tab 1: Single File Process
        self.single_tab = ttk.Frame(self.notebook, padding=12)
        self.notebook.add(self.single_tab, text=" Single File ")
        self._build_single_tab()

        # Tab 2: Batch Processing
        self.batch_tab = ttk.Frame(self.notebook, padding=12)
        self.notebook.add(self.batch_tab, text=" Batch Processing ")
        self._build_batch_tab()

        # Tab 3: Container Inspector / Benchmark
        self.inspector_tab = ttk.Frame(self.notebook, padding=12)
        self.notebook.add(self.inspector_tab, text=" CMP2 Inspector ")
        self._build_inspector_tab()

        # Bottom Log Frame
        log_frame = ttk.LabelFrame(self, text="Activity Log & Output", padding=8)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=16, pady=(0, 8))

        self.log_text = tk.Text(log_frame, height=9, wrap=tk.WORD, font=("Menlo", 10))
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        log_scroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        log_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.config(yscrollcommand=log_scroll.set)

        # Bottom Status Bar & Progress Bar
        status_bar = ttk.Frame(self, padding=(16, 4))
        status_bar.pack(fill=tk.X)

        self.status_var = tk.StringVar(value="Ready")
        self.status_lbl = ttk.Label(status_bar, textvariable=self.status_var, style="Status.TLabel")
        self.status_lbl.pack(side=tk.LEFT)

        self.progress = ttk.Progressbar(status_bar, orient=tk.HORIZONTAL, mode="indeterminate", length=180)
        self.progress.pack(side=tk.RIGHT)

    def _build_single_tab(self):
        # Mode selector radio buttons
        mode_frame = ttk.LabelFrame(self.single_tab, text="Operation Mode", padding=10)
        mode_frame.pack(fill=tk.X, pady=(0, 10))

        self.mode_var = tk.StringVar(value="compress")
        r_comp = ttk.Radiobutton(mode_frame, text="Compress to .cmp2", variable=self.mode_var, value="compress", command=self._on_mode_change)
        r_comp.pack(side=tk.LEFT, padx=16)

        r_decomp = ttk.Radiobutton(mode_frame, text="Decompress from .cmp2", variable=self.mode_var, value="decompress", command=self._on_mode_change)
        r_decomp.pack(side=tk.LEFT, padx=16)

        # Input & Output File Selection
        file_frame = ttk.LabelFrame(self.single_tab, text="File Paths", padding=10)
        file_frame.pack(fill=tk.X, pady=(0, 10))

        # Input
        ttk.Label(file_frame, text="Input File:").grid(row=0, column=0, sticky=tk.W, pady=4)
        self.input_entry = ttk.Entry(file_frame)
        self.input_entry.grid(row=0, column=1, sticky=tk.EW, padx=8, pady=4)
        ttk.Button(file_frame, text="Browse...", command=self._browse_single_input).grid(row=0, column=2, pady=4)

        # Output
        ttk.Label(file_frame, text="Output File:").grid(row=1, column=0, sticky=tk.W, pady=4)
        self.output_entry = ttk.Entry(file_frame)
        self.output_entry.grid(row=1, column=1, sticky=tk.EW, padx=8, pady=4)
        ttk.Button(file_frame, text="Browse...", command=self._browse_single_output).grid(row=1, column=2, pady=4)

        file_frame.columnconfigure(1, weight=1)

        # Options Frame
        opts_frame = ttk.LabelFrame(self.single_tab, text="Compression Options", padding=10)
        opts_frame.pack(fill=tk.X, pady=(0, 10))

        self.window_lbl = ttk.Label(opts_frame, text="LZ77 Window Size:")
        self.window_lbl.grid(row=0, column=0, sticky=tk.W, padx=4)

        self.window_var = tk.StringVar(value="32768")
        self.window_cb = ttk.Combobox(
            opts_frame,
            textvariable=self.window_var,
            values=["1024", "2048", "4096", "8192", "16384", "32768"],
            state="readonly",
            width=10,
        )
        self.window_cb.grid(row=0, column=1, sticky=tk.W, padx=8)
        ttk.Label(opts_frame, text="bytes (default: 32768 = 32 KiB)").grid(row=0, column=2, sticky=tk.W)

        # Action Buttons
        btn_frame = ttk.Frame(self.single_tab)
        btn_frame.pack(fill=tk.X, pady=6)

        self.run_btn = ttk.Button(btn_frame, text="Start Operation", style="Action.TButton", command=self._start_single_operation)
        self.run_btn.pack(side=tk.LEFT, padx=4)

        ttk.Button(btn_frame, text="Clear", command=self._clear_single_tab).pack(side=tk.LEFT, padx=4)

    def _build_batch_tab(self):
        desc = ttk.Label(self.batch_tab, text="Select multiple files or an entire folder to batch compress/decompress.")
        desc.pack(anchor=tk.W, pady=(0, 8))

        top_bar = ttk.Frame(self.batch_tab)
        top_bar.pack(fill=tk.X, pady=(0, 8))

        self.batch_mode_var = tk.StringVar(value="compress")
        ttk.Radiobutton(top_bar, text="Batch Compress", variable=self.batch_mode_var, value="compress").pack(side=tk.LEFT, padx=8)
        ttk.Radiobutton(top_bar, text="Batch Decompress", variable=self.batch_mode_var, value="decompress").pack(side=tk.LEFT, padx=8)

        ttk.Button(top_bar, text="Add Files...", command=self._batch_add_files).pack(side=tk.RIGHT, padx=4)
        ttk.Button(top_bar, text="Add Folder...", command=self._batch_add_folder).pack(side=tk.RIGHT, padx=4)
        ttk.Button(top_bar, text="Clear List", command=self._batch_clear).pack(side=tk.RIGHT, padx=4)

        # Treeview for batch files
        columns = ("path", "size", "status")
        self.batch_tree = ttk.Treeview(self.batch_tab, columns=columns, show="headings", height=8)
        self.batch_tree.heading("path", text="File Path")
        self.batch_tree.heading("size", text="Original Size")
        self.batch_tree.heading("status", text="Status")

        self.batch_tree.column("path", width=420)
        self.batch_tree.column("size", width=110, anchor=tk.E)
        self.batch_tree.column("status", width=150)
        self.batch_tree.pack(fill=tk.BOTH, expand=True, pady=(0, 8))

        self.batch_btn = ttk.Button(self.batch_tab, text="Execute Batch", style="Action.TButton", command=self._start_batch_operation)
        self.batch_btn.pack(anchor=tk.W)

    def _build_inspector_tab(self):
        top_frame = ttk.Frame(self.inspector_tab)
        top_frame.pack(fill=tk.X, pady=(0, 8))

        ttk.Label(top_frame, text="Select CMP2 Container:").pack(side=tk.LEFT, padx=(0, 6))
        self.inspect_entry = ttk.Entry(top_frame)
        self.inspect_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
        ttk.Button(top_frame, text="Browse...", command=self._browse_inspect_file).pack(side=tk.LEFT, padx=4)
        ttk.Button(top_frame, text="Inspect & Benchmark", command=self._inspect_file).pack(side=tk.LEFT, padx=4)

        # Output text box for JSON / benchmark metrics
        self.inspect_text = tk.Text(self.inspector_tab, wrap=tk.WORD, font=("Menlo", 10), height=14)
        self.inspect_text.pack(fill=tk.BOTH, expand=True)

    def _check_binaries(self):
        if not self.compressor_bin:
            self._log("[WARN] 'compressor' binary not found in build directories. Please compile the C++ project using CMake.")
            messagebox.showwarning(
                "Executable Not Found",
                "Could not find the 'compressor' executable.\n"
                "Please run:\n"
                "  mkdir -p build && cd build\n"
                "  cmake -S .. -B . && cmake --build .\n"
                "Or configure the path manually."
            )
        else:
            self._log(f"[INFO] Found compressor binary: {self.compressor_bin}")

        if self.benchmark_bin:
            self._log(f"[INFO] Found benchmark CLI binary: {self.benchmark_bin}")

    def _log(self, text: str):
        self.log_text.insert(tk.END, text + "\n")
        self.log_text.see(tk.END)

    def _format_size(self, size_bytes: int) -> str:
        for unit in ["B", "KiB", "MiB", "GiB"]:
            if size_bytes < 1024:
                return f"{size_bytes:.2f} {unit}" if unit != "B" else f"{size_bytes} B"
            size_bytes /= 1024
        return f"{size_bytes:.2f} TiB"

    # --- Single File Callbacks ---
    def _on_mode_change(self):
        mode = self.mode_var.get()
        inp = self.input_entry.get().strip()
        if mode == "decompress":
            self.window_cb.config(state="disabled")
            self.window_lbl.config(foreground="#888")
            if inp.endswith(".cmp2"):
                self.output_entry.delete(0, tk.END)
                self.output_entry.insert(0, str(Path(inp).with_suffix("")))
        else:
            self.window_cb.config(state="readonly")
            self.window_lbl.config(foreground="#000")
            if inp and not inp.endswith(".cmp2"):
                self.output_entry.delete(0, tk.END)
                self.output_entry.insert(0, inp + ".cmp2")

    def _browse_single_input(self):
        if self.mode_var.get() == "decompress":
            path = filedialog.askopenfilename(title="Select CMP2 Container to Decompress", filetypes=[("CMP2 files", "*.cmp2"), ("All files", "*.*")])
        else:
            path = filedialog.askopenfilename(title="Select File to Compress")

        if path:
            self.input_entry.delete(0, tk.END)
            self.input_entry.insert(0, path)

            # Auto-detect mode based on file extension
            if path.endswith(".cmp2"):
                self.mode_var.set("decompress")
                self.output_entry.delete(0, tk.END)
                # Remove .cmp2 extension or add .restored
                candidate = Path(path).with_suffix("")
                if candidate.exists():
                    candidate = Path(str(candidate) + ".restored")
                self.output_entry.insert(0, str(candidate))
            else:
                self.mode_var.set("compress")
                self.output_entry.delete(0, tk.END)
                self.output_entry.insert(0, path + ".cmp2")
            self._on_mode_change()

    def _browse_single_output(self):
        path = filedialog.asksaveasfilename(title="Select Destination Output File")
        if path:
            self.output_entry.delete(0, tk.END)
            self.output_entry.insert(0, path)

    def _clear_single_tab(self):
        self.input_entry.delete(0, tk.END)
        self.output_entry.delete(0, tk.END)
        self.status_var.set("Ready")

    def _start_single_operation(self):
        if not self.compressor_bin:
            messagebox.showerror("Error", "Compressor executable was not found. Please build the C++ project.")
            return

        inp = self.input_entry.get().strip()
        out = self.output_entry.get().strip()
        mode = self.mode_var.get()
        window = self.window_var.get().strip()

        if not inp or not os.path.exists(inp):
            messagebox.showerror("Invalid Input", "Please choose an existing input file.")
            return
        if not out:
            messagebox.showerror("Invalid Output", "Please choose an output file path.")
            return

        cmd = [str(self.compressor_bin)]
        if mode == "compress":
            cmd.extend(["-c", inp, "-o", out, "-w", window])
        else:
            cmd.extend(["-d", inp, "-o", out])

        self.run_btn.config(state="disabled")
        self.progress.start(10)
        self.status_var.set(f"Executing {mode}...")
        self._log(f"\n[EXEC] {' '.join(cmd)}")

        def worker():
            try:
                proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                success = proc.returncode == 0
                out_msg = proc.stdout.strip()
                err_msg = proc.stderr.strip()
            except Exception as e:
                success = False
                out_msg = ""
                err_msg = str(e)

            self.after(0, lambda: self._on_single_done(success, mode, inp, out, out_msg, err_msg))

        threading.Thread(target=worker, daemon=True).start()

    def _on_single_done(self, success: bool, mode: str, inp: str, out: str, out_msg: str, err_msg: str):
        self.progress.stop()
        self.run_btn.config(state="normal")

        if out_msg:
            self._log(f"[STDOUT] {out_msg}")
        if err_msg:
            self._log(f"[STDERR] {err_msg}")

        if success:
            self.status_var.set(f"Success! {mode.capitalize()} completed.")
            if os.path.exists(out):
                in_size = os.path.getsize(inp)
                out_size = os.path.getsize(out)
                ratio_str = ""
                if mode == "compress" and in_size > 0:
                    pct = ((in_size - out_size) / in_size) * 100
                    ratio = in_size / out_size if out_size > 0 else 0
                    ratio_str = f" | Ratio: {ratio:.2f}x (saved {pct:.1f}%)"
                self._log(f"[DONE] Output: {out} ({self._format_size(out_size)}){ratio_str}")
            messagebox.showinfo("Success", f"{mode.capitalize()} finished successfully!")
        else:
            self.status_var.set(f"Error during {mode}.")
            messagebox.showerror("Execution Failed", err_msg or "Process failed. Check activity log.")

    # --- Batch Tab Callbacks ---
    def _batch_add_files(self):
        paths = filedialog.askopenfilenames(title="Select Files for Batch Processing")
        for p in paths:
            sz = self._format_size(os.path.getsize(p))
            self.batch_tree.insert("", tk.END, values=(p, sz, "Pending"))

    def _batch_add_folder(self):
        folder = filedialog.askdirectory(title="Select Folder to Batch Process")
        if folder:
            for root, _, files in os.walk(folder):
                for f in files:
                    if f.startswith("."):
                        continue
                    p = os.path.join(root, f)
                    sz = self._format_size(os.path.getsize(p))
                    self.batch_tree.insert("", tk.END, values=(p, sz, "Pending"))

    def _batch_clear(self):
        for item in self.batch_tree.get_children():
            self.batch_tree.delete(item)

    def _start_batch_operation(self):
        items = self.batch_tree.get_children()
        if not items:
            messagebox.showwarning("Empty List", "Please add files to the batch list first.")
            return
        if not self.compressor_bin:
            messagebox.showerror("Error", "Compressor executable not found.")
            return

        mode = self.batch_mode_var.get()
        self.batch_btn.config(state="disabled")
        self.progress.start(10)
        self.status_var.set(f"Running batch {mode}...")

        def worker():
            for item in items:
                vals = self.batch_tree.item(item, "values")
                src = vals[0]
                if mode == "compress":
                    dst = src + ".cmp2"
                    cmd = [str(self.compressor_bin), "-c", src, "-o", dst]
                else:
                    dst = str(Path(src).with_suffix("")) if src.endswith(".cmp2") else src + ".out"
                    cmd = [str(self.compressor_bin), "-d", src, "-o", dst]

                self.after(0, lambda it=item: self.batch_tree.set(it, "status", "Processing..."))
                proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                status = "Done" if proc.returncode == 0 else "Failed"
                self.after(0, lambda it=item, st=status: self.batch_tree.set(it, "status", st))

            self.after(0, self._on_batch_done)

        threading.Thread(target=worker, daemon=True).start()

    def _on_batch_done(self):
        self.progress.stop()
        self.batch_btn.config(state="normal")
        self.status_var.set("Batch processing finished.")
        messagebox.showinfo("Batch Complete", "All batch operations have finished.")

    # --- Inspector / Benchmark Callbacks ---
    def _browse_inspect_file(self):
        path = filedialog.askopenfilename(title="Select CMP2 File to Inspect", filetypes=[("CMP2 Files", "*.cmp2"), ("All Files", "*.*")])
        if path:
            self.inspect_entry.delete(0, tk.END)
            self.inspect_entry.insert(0, path)
            self._inspect_file()

    def _inspect_file(self):
        path = self.inspect_entry.get().strip()
        if not path or not os.path.exists(path):
            messagebox.showerror("Error", "Please select a valid .cmp2 file.")
            return

        self.inspect_text.delete("1.0", tk.END)

        if not self.benchmark_bin:
            self.inspect_text.insert(tk.END, "benchmark_cli binary not found. Cannot inspect container.")
            return

        cmd = [str(self.benchmark_bin), path, "--json"]
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res.returncode == 0:
                try:
                    data = json.loads(res.stdout)
                    pretty = json.dumps(data, indent=2)
                    self.inspect_text.insert(tk.END, pretty)
                except Exception:
                    self.inspect_text.insert(tk.END, res.stdout)
            else:
                self.inspect_text.insert(tk.END, f"Error inspecting file:\n{res.stderr}")
        except Exception as e:
            self.inspect_text.insert(tk.END, f"Exception: {e}")


def main():
    app = CompressorGUI()
    app.mainloop()


if __name__ == "__main__":
    main()
