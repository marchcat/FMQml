#pragma once
#include <memory>
class FileProvider;
std::unique_ptr<FileProvider> createMegaFileProvider();
