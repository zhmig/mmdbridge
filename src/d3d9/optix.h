#pragma once

#include <string>

void InitOptix();
void StartOptix(int setting, int currentframe);
void UpdateOptix(int currentframe);
void ChangeOptixRenderType(int renderType);
BOOL LoadHDRI(const std::wstring& hdriPath);
void EnableHDRI(bool enable);
void UpdateOptixGeometry();
void RemoveGeometry();
void DisposeOptix();
