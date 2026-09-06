#include <tinyxml2.h>
#include <cstdio>
#include <malloc.h>

#include "soh/resource/importer/TextFactory.h"
#include "soh/resource/type/Text.h"

namespace SOH {
std::shared_ptr<Ship::IResource>
ResourceFactoryBinaryTextV0::ReadResource(std::shared_ptr<Ship::File> file,
                                          std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto text = std::make_shared<Text>(initData);
    auto reader = std::get<std::shared_ptr<Ship::BinaryReader>>(file->Reader);

    uint32_t msgCount = reader->ReadUInt32();
#ifdef __3DS__
    // SoH-3DS: a wrong buffer offset here yields a garbage count, and reserve()
    // on a garbage count is an allocation failure - which stalls on this target
    // instead of throwing. Print it before committing to the allocation.
    {
        struct mallinfo mi = mallinfo();
        fprintf(stderr, "soh-3ds text: msgCount=%u used=%uKiB free=%uKiB reserving %uB\n", (unsigned)msgCount,
                (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024),
                (unsigned)(msgCount * sizeof(MessageEntry)));
        fflush(stderr);
    }
#endif
    text->messages.reserve(msgCount);

    for (uint32_t i = 0; i < msgCount; i++) {
        MessageEntry entry;
        entry.id = reader->ReadUInt16();
        entry.textboxType = reader->ReadUByte();
        entry.textboxYPos = reader->ReadUByte();
        entry.msg = reader->ReadString();

        text->messages.push_back(entry);
    }

    return text;
}

std::shared_ptr<Ship::IResource>
ResourceFactoryXMLTextV0::ReadResource(std::shared_ptr<Ship::File> file,
                                       std::shared_ptr<Ship::ResourceInitData> initData) {
    if (!FileHasValidFormatAndReader(file, initData)) {
        return nullptr;
    }

    auto txt = std::make_shared<Text>(initData);
    auto child =
        std::get<std::shared_ptr<tinyxml2::XMLDocument>>(file->Reader)->FirstChildElement()->FirstChildElement();

    while (child != nullptr) {
        std::string childName = child->Name();

        if (childName == "TextEntry") {
            MessageEntry entry;
            entry.id = child->IntAttribute("ID");
            entry.textboxType = child->IntAttribute("TextboxType");
            entry.textboxYPos = child->IntAttribute("TextboxYPos");
            entry.msg = child->Attribute("Message");
            entry.msg += "\x2";

            txt->messages.push_back(entry);
            int bp = 0;
        }

        child = child->NextSiblingElement();
    }

    return txt;
}
} // namespace SOH
