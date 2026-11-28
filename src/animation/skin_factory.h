#pragma once

#include "animation/pet_skin.h"

#include <memory>

class Config;

// 皮肤工厂：按 config.json 的 appearance.animation 选择实现。
// Lottie 加载失败（未编译 rlottie、素材缺失）时自动退回内置图片皮肤，日志里说明原因。
std::unique_ptr<PetSkin> createSkin(const Config& config, QString* error);