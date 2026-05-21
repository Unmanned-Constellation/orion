# Orin Setup

One-time configuration required after flashing JetPack on a new Orin Nano.

## 1. Set the NVIDIA runtime as Docker default

JetPack registers the NVIDIA container runtime but leaves `runc` as Docker's default. Without this
change, containers (including the devcontainer) will not have GPU access.

```bash
sudo jq '. + {"default-runtime": "nvidia"}' /etc/docker/daemon.json \
    | sudo tee /etc/docker/daemon.json.tmp \
    && sudo mv /etc/docker/daemon.json.tmp /etc/docker/daemon.json

sudo systemctl restart docker
```

## 2. Clone the repo and open in VS Code

```bash
git clone <repo-url> orion
code orion
```

VS Code will detect `.devcontainer/devcontainer.json` and prompt to reopen in container. The
Dockerfile auto-detects `arm64` and uses the DeepStream L4T base image. The first build will take
a while as it pulls the base image and compiles Conan dependencies.

## 3. Verify GPU access

Inside the devcontainer:

```bash
nvidia-smi
```
