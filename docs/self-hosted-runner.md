# Self-hosted runner setup (build the PortMaster zip via GitHub Actions)

This project statically recompiles *your own* Super Mario World (USA) ROM
into C code at build time (`src/gen/`, produced by `tools/regen.sh`). That
ROM can never be committed to this repo, uploaded as a GitHub Actions
secret, or otherwise sent to GitHub's infrastructure - Nintendo owns the
copyright, and "just a secret" still means storing the ROM on a third
party's servers, which nothing here is willing to do on your behalf.

The workaround: a **self-hosted GitHub Actions runner** is a small agent
you install and register on *your own* machine. GitHub's servers only send
it job instructions; the runner executes them locally, using files already
on that machine (your ROM included) that never travel over the network to
GitHub. `.github/workflows/self-hosted-package.yml` is written to only ever
run there, and only uploads the finished `.zip` (which never contains a
ROM - the workflow actively checks and refuses to upload one that does).

This is written for a plain Linux x86_64 desktop (e.g. Arch Linux), since
that's the common case and the H700 itself has nowhere near enough storage
or RAM to reasonably build this project. If your runner machine is already
aarch64 (a Raspberry Pi, another SBC, an ARM cloud VM), skip the QEMU step.

## 1. Install Docker

```bash
# Arch Linux / Omarchy
sudo pacman -S docker
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
# log out and back in (or `newgrp docker`) for the group change to apply
```

(Debian/Ubuntu: `sudo apt install docker.io`, then the same `systemctl`/
`usermod` lines.)

## 2. Register aarch64 emulation (skip if your runner is already aarch64)

```bash
docker run --rm --privileged tonistiigi/binfmt --install arm64
```

This registers `binfmt_misc` handlers so `docker run --platform linux/arm64
...` transparently runs real aarch64 binaries under QEMU. It's a one-time
setup per boot of the host kernel (some distros need it re-run after a
reboot; if `self-hosted-package.yml` suddenly fails with an "exec format
error", re-run this command).

## 3. Register the self-hosted runner against this repo

1. On GitHub: this repo → **Settings → Actions → Runners → New
   self-hosted runner**. Pick Linux/x64.
2. Follow the page's own download + `./config.sh --url ... --token ...`
   commands (the token is short-lived and generated per-runner, so copy it
   from that page rather than reusing one from here).
3. When `config.sh` asks for labels, add `h700-builder` (matches
   `runs-on: [self-hosted, h700-builder]` in the workflow - if you use a
   different label, update the workflow file to match).
4. Run it either in the foreground (`./run.sh`, only builds while that
   terminal is open) or install it as a systemd service so it's always
   available: `sudo ./svc.sh install && sudo ./svc.sh start`.

If you're also setting up the sibling `FZeroSNESRecomp` fork's runner, you
can reuse the same physical machine, but GitHub's per-repo runner
registration on a personal account means you'll register a second,
separate runner instance there (a different folder, its own `config.sh`
run against that repo) - the Docker/QEMU setup above only needs doing once
per machine.

## 4. Put your ROM somewhere on that machine

Anywhere readable by the account the runner service runs as, e.g.
`/home/you/roms/smw.sfc`. You'll pass this exact path as the `rom_path`
input when you run the workflow - nothing here scans for it automatically.

## 5. Run the workflow

GitHub → this repo → **Actions → PortMaster zip (aarch64, self-hosted) →
Run workflow**, fill in `rom_path`, run it. The finished zip appears as a
downloadable artifact on the run's summary page once it's done - expect the
first run to take a while (QEMU-emulated compilation, a from-source SDL3
build, plus the asar/SMWDisX-driven ROM regeneration step, is not fast),
later runs will be quicker if Docker's layer/build caches survive between
runs on that machine.
