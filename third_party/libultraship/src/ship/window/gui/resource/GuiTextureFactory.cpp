#include "ship/window/gui/resource/GuiTextureFactory.h"
#include "ship/window/gui/resource/GuiTexture.h"
#include "spdlog/spdlog.h"

namespace Ship {
std::shared_ptr<IResource>
ResourceFactoryBinaryGuiTextureV0::ReadResource(std::shared_ptr<File> file,
                                                std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto guiTexture = std::make_shared<GuiTexture>(initData);
    auto reader = std::get<std::shared_ptr<BinaryReader>>(file->Reader);

    guiTexture->DataSize = file->Buffer->size();
    guiTexture->Metadata.Width = 0;
    guiTexture->Metadata.Height = 0;
    // SoH-3DS: stb_image writes through int*, but Metadata.{Width,Height} are
    // int32_t, which is not int on devkitARM. Take the values in ints and copy.
    int stbWidth = 0;
    int stbHeight = 0;
    guiTexture->Data = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(file->Buffer->data()),
                                             guiTexture->DataSize, &stbWidth, &stbHeight, nullptr, 4);
    guiTexture->Metadata.Width = stbWidth;
    guiTexture->Metadata.Height = stbHeight;

    if (guiTexture->Data == nullptr) {
        SPDLOG_ERROR("Error loading imgui texture {}", stbi_failure_reason());
        return nullptr;
    }

    return guiTexture;
}
} // namespace Ship
