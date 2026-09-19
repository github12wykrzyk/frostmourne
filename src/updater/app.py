"""Windows Tk GUI for the standalone FROSTMOURNE updater."""
from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from src.updater.core import (
    GithubSource, TARGET_SHA256, Updater, UpdateError, check_client,
    digest, game_running, validate_manifest,
)

DEFAULT_GAME = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "Whitemane" / "Games" / "FrostmourneRebuffed"


class App:
    def __init__(self):
        self.window = tk.Tk()
        self.window.title("FROSTMOURNE Updater • WoW 3.3.5a x86")
        self.window.geometry("760x630")
        self.window.minsize(650, 530)
        self.install = tk.StringVar(value=str(DEFAULT_GAME))
        self.token = tk.StringVar()
        self.release = None
        self.selected = {}
        self.events = queue.Queue()
        self.busy = False
        self._layout()
        self.window.after(100, self._drain)
        self.window.protocol("WM_DELETE_WINDOW", self._close)

    def _layout(self):
        w = self.window
        frame = ttk.Frame(w, padding=14)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="FROSTMOURNE", font=("Segoe UI", 19, "bold")).pack(anchor="w")
        ttk.Label(frame, text="Whitemane • World of Warcraft 3.3.5a • build 12340 • Windows x86").pack(anchor="w", pady=(0, 12))
        pathline = ttk.Frame(frame)
        pathline.pack(fill="x")
        ttk.Label(pathline, text="Folder gry:").pack(side="left")
        ttk.Entry(pathline, textvariable=self.install).pack(side="left", fill="x", expand=True, padx=8)
        ttk.Button(pathline, text="Wybierz…", command=self._browse).pack(side="left")
        keyline = ttk.Frame(frame)
        keyline.pack(fill="x", pady=9)
        ttk.Label(keyline, text="Token GitHub:").pack(side="left")
        ttk.Entry(keyline, textvariable=self.token, show="•").pack(side="left", fill="x", expand=True, padx=8)
        ttk.Label(keyline, text="tylko bieżąca sesja").pack(side="left")
        ttk.Label(frame, text="Repozytorium jest prywatne. Token wymaga tylko dostępu do odczytu jego zawartości.",
                  wraplength=690).pack(anchor="w", pady=(0, 8))
        self.status = tk.StringVar(value="Wybierz folder gry i sprawdź dostępne wydanie na gałęzi work.")
        ttk.Label(frame, textvariable=self.status, wraplength=690).pack(anchor="w", pady=3)
        buttons = ttk.Frame(frame)
        buttons.pack(fill="x", pady=9)
        self.check_button = ttk.Button(buttons, text="Sprawdź aktualizacje", command=self._check)
        self.check_button.pack(side="left", padx=(0, 6))
        self.install_button = ttk.Button(buttons, text="Aktualizuj zaznaczone", command=self._apply, state="disabled")
        self.install_button.pack(side="left", padx=6)
        self.rollback_button = ttk.Button(buttons, text="Rollback", command=self._rollback)
        self.rollback_button.pack(side="left", padx=6)
        ttk.Button(buttons, text="Uruchom Wow.exe", command=self._launch).pack(side="left", padx=6)
        ttk.Label(frame, text="Pliki wydania (odznaczenie jest możliwe tylko przy zgodnym pliku lokalnym):").pack(anchor="w")
        listframe = ttk.Frame(frame)
        listframe.pack(fill="both", expand=True, pady=5)
        self.canvas = tk.Canvas(listframe, borderwidth=0, height=175, highlightthickness=0)
        sb = ttk.Scrollbar(listframe, orient="vertical", command=self.canvas.yview)
        self.listinner = ttk.Frame(self.canvas)
        self.listinner.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.create_window((0, 0), window=self.listinner, anchor="nw")
        self.canvas.configure(yscrollcommand=sb.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        ttk.Label(frame, text="Dziennik operacji:").pack(anchor="w")
        logframe = ttk.Frame(frame)
        logframe.pack(fill="both", expand=True)
        self.log = tk.Text(logframe, height=9, wrap="word", state="disabled")
        self.log.pack(fill="both", expand=True)
        footer = ttk.Frame(frame)
        footer.pack(fill="x", pady=(8, 0))
        ttk.Button(footer, text="Zapisz raport lokalny", command=self._report).pack(side="left")
        ttk.Button(footer, text="Otwórz katalog gry", command=self._open_folder).pack(side="left", padx=8)
        ttk.Label(footer, text="Brak automatycznego wysyłania danych.").pack(side="right")

    def _write(self, message):
        self.log.configure(state="normal")
        self.log.insert("end", message + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")
        self.status.set(message)

    def _drain(self):
        while True:
            try:
                kind, value = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == "log":
                self._write(value)
            elif kind == "release":
                self.release = value
                self._render_release(value)
                self._write("Wydanie: " + str(value.get("release_id") or "brak aktywnego builda"))
            elif kind == "done":
                self.busy = False
                self.check_button.configure(state="normal")
                self.rollback_button.configure(state="normal")
                self.install_button.configure(state="normal" if self.release and self.release["files"] else "disabled")
            elif kind == "error":
                self._write("BŁĄD: " + value)
                messagebox.showerror("FROSTMOURNE", value)
        self.window.after(100, self._drain)

    def _worker(self, fn):
        if self.busy:
            return
        self.busy = True
        self.check_button.configure(state="disabled")
        self.install_button.configure(state="disabled")
        self.rollback_button.configure(state="disabled")

        def run():
            try:
                fn()
            except Exception as exc:
                self.events.put(("error", str(exc)))
            finally:
                self.events.put(("done", None))
        threading.Thread(target=run, daemon=True).start()

    def _browse(self):
        directory = filedialog.askdirectory(initialdir=self.install.get(), title="Wskaż folder zawierający Wow.exe")
        if directory:
            self.install.set(directory)
            self.release = None
            self._render_release({"files": []})
            self.install_button.configure(state="disabled")

    def _check(self):
        install, token = self.install.get(), self.token.get()
        self.release = None
        self.install_button.configure(state="disabled")

        def task():
            updater = Updater(install)
            if updater.recover():
                self.events.put(("log", "Odzyskano przerwaną aktualizację."))
            check_client(updater.root)
            self.events.put(("log", "Klient WoW.exe: zgodny SHA256, build 12340 x86."))
            manifest = GithubSource(token, ref="work").fetch_manifest()
            self.events.put(("release", manifest))
            if not manifest["files"]:
                self.events.put(("log", "Repozytorium nie opublikowało jeszcze plików gry do aktualizacji."))
        self._worker(task)

    def _render_release(self, manifest):
        for child in self.listinner.winfo_children():
            child.destroy()
        self.selected = {}
        if not manifest["files"]:
            ttk.Label(self.listinner, text="Brak plików w bieżącym wydaniu.").pack(anchor="w")
            return
        for item in manifest["files"]:
            comp = item["component"]
            var = tk.BooleanVar(value=True)
            self.selected[comp] = var
            ttk.Checkbutton(self.listinner, variable=var, text="{}  •  {}  •  {}".format(
                comp, item["version"], item["path"])).pack(anchor="w", pady=2)

    def _apply(self):
        if not self.release or not self.release["files"]:
            messagebox.showinfo("FROSTMOURNE", "Nie ma jeszcze opublikowanego builda.")
            return
        manifest = self.release
        chosen = [key for key, var in self.selected.items() if var.get()]
        install, token = self.install.get(), self.token.get()
        if not messagebox.askyesno("Aktualizacja", "Zainstalować wydanie {}?\nGra musi być zamknięta.".format(manifest["release_id"])):
            return

        def task():
            source = GithubSource(token, ref="work")
            # Re-fetch and compare the complete manifest to prevent stale UI selections.
            fresh = source.fetch_manifest()
            if fresh != manifest:
                raise UpdateError("Manifest zmienił się od sprawdzenia. Sprawdź aktualizacje ponownie.")
            updater = Updater(install)
            updater.apply(manifest, chosen, source, lambda message: self.events.put(("log", message)))
        self._worker(task)

    def _rollback(self):
        install = self.install.get()
        if not messagebox.askyesno("Rollback", "Przywrócić poprzednią spójną wersję plików? Gra musi być zamknięta."):
            return

        def task():
            updater = Updater(install)
            restored = updater.rollback()
            self.events.put(("log", "Przywrócono stan poprzedzający wydanie " + str(restored)))
        self._worker(task)

    def _launch(self):
        try:
            if self.busy:
                raise UpdateError("Zakończ aktualną operację przed uruchomieniem gry.")
            updater = Updater(self.install.get())
            if updater.recover():
                self._write("Odzyskano przerwaną aktualizację.")
            client = check_client(updater.root)
            if game_running(updater.root):
                raise UpdateError("Gra jest już uruchomiona.")
            subprocess.Popen([str(client)], cwd=str(updater.root), close_fds=True)
            self._write("Uruchomiono klienta (bez omijania oficjalnych kontroli launchera).")
        except Exception as exc:
            messagebox.showerror("Uruchamianie", str(exc))

    def _open_folder(self):
        try:
            p = Path(self.install.get()).resolve(strict=True)
            if not p.is_dir():
                raise UpdateError("Katalog gry nie istnieje.")
            os.startfile(str(p))
        except Exception as exc:
            messagebox.showerror("Katalog", str(exc))

    def _report(self):
        filename = filedialog.asksaveasfilename(defaultextension=".json", filetypes=[("JSON", "*.json")],
                                                initialfile="frostmourne-report.json")
        if not filename:
            return
        try:
            updater = Updater(self.install.get())
            wow = updater.root / "Wow.exe"
            installed = json.loads(updater.installed.read_text(encoding="utf-8")) if updater.installed.is_file() else None
            report = {"updater": "0.1.0", "client_expected_sha256": TARGET_SHA256,
                      "client_actual_sha256": digest(wow) if wow.is_file() else None,
                      "release_id": installed.get("release_id") if installed else None,
                      "files": [{"component": f["component"], "version": f["version"], "sha256": f["sha256"]}
                                for f in installed.get("files", [])] if installed else [],
                      "journal_exists": updater.journal.exists(),
                      "log": self.log.get("1.0", "end").strip()}
            Path(filename).write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
            self._write("Zapisano lokalny raport diagnostyczny.")
        except Exception as exc:
            messagebox.showerror("Raport", str(exc))

    def _close(self):
        if self.busy:
            messagebox.showwarning("Operacja trwa", "Nie zamykaj updatera podczas zapisu plików.")
            return
        self.token.set("")
        self.window.destroy()

    def run(self):
        self.window.mainloop()


def main():
    App().run()
