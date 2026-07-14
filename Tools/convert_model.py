#!/usr/bin/env python3
"""Converts Stable Diffusion checkpoints to the split ONNX layout used by the app.

Downloads a checkpoint, exports it with optimum, converts the weights to fp16
and emits flat release assets plus a manifest describing the file layout.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import urllib.request

PRESETS = {
    "counterfeit-v3": {
        "key": "counterfeit-v3",
        "name": "Counterfeit V3 (anime)",
        "website": "https://huggingface.co/gsdf/Counterfeit-V3.0",
        "checkpoint_url": "https://huggingface.co/gsdf/Counterfeit-V3.0/resolve/main/Counterfeit-V3.0_fp16.safetensors",
    },
    "sd15": {
        "key": "sd15",
        "name": "Stable Diffusion 1.5",
        "website": "https://huggingface.co/stable-diffusion-v1-5/stable-diffusion-v1-5",
        "diffusers_repo": "stable-diffusion-v1-5/stable-diffusion-v1-5",
    },
}

MODEL_FILES = [
    "unet/model.onnx",
    "text_encoder/model.onnx",
    "vae_encoder/model.onnx",
    "vae_decoder/model.onnx",
    "tokenizer/vocab.json",
    "tokenizer/merges.txt",
    "tokenizer/special_tokens_map.json",
    "tokenizer/tokenizer_config.json",
    "scheduler/scheduler_config.json",
]

ONNX_COMPONENTS = ["unet", "text_encoder", "vae_encoder", "vae_decoder"]


def log(message):
    print(f"[convert] {message}", flush=True)


def download(url, path):
    log(f"downloading {url}")
    request = urllib.request.Request(url, headers={"User-Agent": "unpaint-tools/1.0"})
    with urllib.request.urlopen(request) as response, open(path, "wb") as target:
        shutil.copyfileobj(response, target, length=1024 * 1024)
    log(f"downloaded {os.path.getsize(path)} bytes")


def load_diffusers_model(preset, work_dir):
    import torch
    from diffusers import StableDiffusionPipeline

    model_dir = os.path.join(work_dir, "diffusers_model")

    if "checkpoint_url" in preset:
        checkpoint = os.path.join(work_dir, "checkpoint.safetensors")
        download(preset["checkpoint_url"], checkpoint)
        log("loading checkpoint into a diffusers pipeline")
        pipe = StableDiffusionPipeline.from_single_file(
            checkpoint,
            torch_dtype=torch.float32,
            safety_checker=None,
            requires_safety_checker=False,
        )
        os.remove(checkpoint)
    else:
        log(f"loading diffusers repo {preset['diffusers_repo']}")
        pipe = StableDiffusionPipeline.from_pretrained(
            preset["diffusers_repo"],
            torch_dtype=torch.float32,
            safety_checker=None,
            requires_safety_checker=False,
        )

    log("saving diffusers model")
    pipe.save_pretrained(model_dir, safe_serialization=True)
    del pipe
    return model_dir


def export_onnx(model_dir, work_dir):
    onnx_dir = os.path.join(work_dir, "onnx_model")
    log("exporting to onnx with optimum")

    commands = [
        [sys.executable, "-m", "optimum.exporters.onnx", "--model", model_dir, onnx_dir],
        [sys.executable, "-m", "optimum.exporters.onnx", "--model", model_dir, "--task", "text-to-image", onnx_dir],
        [sys.executable, "-m", "optimum.exporters.onnx", "--model", model_dir, "--task", "stable-diffusion", onnx_dir],
    ]

    last_error = None
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            shutil.rmtree(model_dir, ignore_errors=True)
            return onnx_dir
        last_error = result.stderr[-4000:]
        log(f"export attempt failed, trying next task name...")

    print(last_error, file=sys.stderr)
    raise RuntimeError("onnx export failed")


def convert_fp16(onnx_dir):
    from onnxruntime.transformers.optimizer import optimize_model
    from onnxruntime.transformers.fusion_options import FusionOptions

    model_types = {
        "unet": "unet",
        "text_encoder": "clip",
        "vae_encoder": "vae",
        "vae_decoder": "vae",
    }

    for component in ONNX_COMPONENTS:
        component_dir = os.path.join(onnx_dir, component)
        model_path = os.path.join(component_dir, "model.onnx")
        if not os.path.exists(model_path):
            raise RuntimeError(f"missing exported component: {component}")

        log(f"optimizing and converting {component} to fp16")
        model_type = model_types[component]
        fusion_options = FusionOptions(model_type)
        model = optimize_model(
            model_path,
            model_type=model_type,
            opt_level=0,
            optimization_options=fusion_options,
            use_gpu=True,
        )
        model.convert_float_to_float16(keep_io_types=True, op_block_list=["RandomNormalLike"])

        # drop the fp32 files and save a single fp16 file
        fp16_path = os.path.join(component_dir, "model_fp16.onnx")
        model.save_model_to_file(fp16_path, use_external_data_format=False)
        del model

        for name in os.listdir(component_dir):
            full = os.path.join(component_dir, name)
            if name != "model_fp16.onnx":
                os.remove(full)
        os.rename(fp16_path, model_path)

        log(f"{component} is now {os.path.getsize(model_path)} bytes")


def verify(onnx_dir):
    import onnxruntime

    for component in ONNX_COMPONENTS:
        model_path = os.path.join(onnx_dir, component, "model.onnx")
        log(f"verifying {component} loads")
        session = onnxruntime.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        del session


def emit_assets(preset, onnx_dir, output_dir):
    assets_dir = os.path.join(output_dir, "assets")
    os.makedirs(assets_dir, exist_ok=True)

    repo = os.environ.get("GITHUB_REPOSITORY", "day7779/unpaint")
    base_url = f"https://github.com/{repo}/releases/download/models-v1"

    key = preset["key"]
    files = []
    for relative_path in MODEL_FILES:
        source = os.path.join(onnx_dir, relative_path)
        if not os.path.exists(source):
            raise RuntimeError(f"missing model file: {relative_path}")

        asset_name = f"{key}.{relative_path.replace('/', '.')}"
        target = os.path.join(assets_dir, asset_name)
        shutil.copyfile(source, target)

        files.append({
            "path": relative_path,
            "url": f"{base_url}/{asset_name}",
            "size": os.path.getsize(target),
        })

    manifest = {
        "id": f"curated/{key}",
        "name": preset["name"],
        "website": preset.get("website", ""),
        "files": files,
    }

    manifest_path = os.path.join(output_dir, f"{key}.manifest.json")
    with open(manifest_path, "w") as target:
        json.dump(manifest, target, indent=2)

    log(f"wrote {manifest_path}")
    log(json.dumps(manifest, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preset", required=True)
    parser.add_argument("--custom-url", default="")
    parser.add_argument("--custom-key", default="")
    parser.add_argument("--output", default="out")
    args = parser.parse_args()

    if args.preset == "custom":
        if not args.custom_url or not args.custom_key:
            raise SystemExit("custom preset needs --custom-url and --custom-key")
        preset = {
            "key": args.custom_key,
            "name": args.custom_key,
            "website": "",
            "checkpoint_url": args.custom_url,
        }
    else:
        preset = PRESETS[args.preset]

    work_dir = "work"
    os.makedirs(work_dir, exist_ok=True)
    os.makedirs(args.output, exist_ok=True)

    model_dir = load_diffusers_model(preset, work_dir)
    onnx_dir = export_onnx(model_dir, work_dir)
    convert_fp16(onnx_dir)
    verify(onnx_dir)
    emit_assets(preset, onnx_dir, args.output)
    log("done")


if __name__ == "__main__":
    main()
