#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

#include "soh/resource/type/Text.h"

extern "C" {
#include <message_data_static.h>
}

extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;
extern "C" MessageTableEntry* sStaffMessageEntryTablePtr;
// extern "C" MessageTableEntry* _message_0xFFFC_nes;

static void SetMessageEntry(MessageTableEntry& entry, const SOH::MessageEntry& msgEntry) {
    entry.textId = msgEntry.id;
    entry.typePos = (msgEntry.textboxType << 4) | msgEntry.textboxYPos;
    entry.segment = msgEntry.msg.c_str();
    entry.msgSize = static_cast<u32>(msgEntry.msg.size());
}

static void OTRMessage_LoadCustom(const std::string& folderPath, MessageTableEntry*& table, size_t tableSize) {
    auto lst = *Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->ListFiles(folderPath).get();

    for (auto& tPath : lst) {
        auto file = std::static_pointer_cast<SOH::Text>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(tPath));
        if (file == nullptr) {
            // SoH-3DS: load failures degrade to null (heap ceiling); same
            // predicted-deref class as crash_dump_00000020.
            std::fprintf(stderr, "msg: null override %s, skipping\n", tPath.c_str());
            continue;
        }

        for (size_t j = 0; j < file->messages.size(); ++j) {
            // Check if same text ID exists already
            auto existingEntry = std::find_if(table, table + tableSize, [id = file->messages[j].id](const auto& entry) {
                return entry.textId == id;
            });

            if (existingEntry != table + tableSize) {
                // Replace existing message
                SetMessageEntry(*existingEntry, file->messages[j]);
            }
        }
    }
}

#ifdef __3DS__
#define MSG3DS_TRACE(...) (fprintf(stderr, "soh-3ds msg: " __VA_ARGS__), fputc('\n', stderr))
#else
#define MSG3DS_TRACE(...) ((void)0)
#endif

MessageTableEntry* OTRMessage_LoadTable(const std::string& filePath, bool isNES) {
    MSG3DS_TRACE("LoadTable %s: load", filePath.c_str());
    auto file = std::static_pointer_cast<SOH::Text>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(filePath));
    MSG3DS_TRACE("LoadTable %s: loaded (%s)", filePath.c_str(), file == nullptr ? "null" : "ok");

    if (file == nullptr)
        return nullptr;

    // Allocate room for an additional message
    // OTRTODO: Should not be malloc'ing here. It's fine for now since we check elsewhere that the message table is
    // already null.
    // SoH-3DS: the comment promised the extra slot but the malloc never had it,
    // while LoadCustom below searches messages.size() + 1 entries - a one-entry
    // heap overread, and a 16-byte overwrite if the garbage matched an id.
    MessageTableEntry* table = (MessageTableEntry*)malloc(sizeof(MessageTableEntry) * (file->messages.size() + 1));
    memset(&table[file->messages.size()], 0, sizeof(MessageTableEntry));

    for (size_t i = 0; i < file->messages.size(); i++) {
        SetMessageEntry(table[i], file->messages[i]);

        if (isNES && file->messages[i].id == 0xFFFC)
            _message_0xFFFC_nes = (char*)file->messages[i].msg.c_str();
    }
    MSG3DS_TRACE("LoadTable %s: entries copied, wildcard override next", filePath.c_str());
    OTRMessage_LoadCustom("override/" + filePath.substr(0, filePath.find_last_of('/')) + "/*", table,
                          file->messages.size() + 1);
    MSG3DS_TRACE("LoadTable %s: override done", filePath.c_str());

    // Assert that the first message starts at the first text ID
    assert(table[0].textId == 0x0001);

    return table;
}

extern "C" void OTRMessage_Init() {
    // OTRTODO: Added a lot of null checks here so that we don't malloc the table multiple times causing a memory leak.
    // We really ought to fix the implementation such that we aren't malloc'ing new tables.
    // Once we fix the implementation, remove these NULL checks.
    if (sNesMessageEntryTablePtr == NULL) {
        sNesMessageEntryTablePtr = OTRMessage_LoadTable("text/nes_message_data_static/nes_message_data_static", true);
    }
    if (sGerMessageEntryTablePtr == NULL) {
        sGerMessageEntryTablePtr = OTRMessage_LoadTable("text/ger_message_data_static/ger_message_data_static", false);
    }
    if (sFraMessageEntryTablePtr == NULL) {
        sFraMessageEntryTablePtr = OTRMessage_LoadTable("text/fra_message_data_static/fra_message_data_static", false);
    }
    if (sJpnMessageEntryTablePtr == NULL) {
        sJpnMessageEntryTablePtr = OTRMessage_LoadTable("text/jpn_message_data_static/jpn_message_data_static", false);
    }
    // Note: Make sure this loads after PAL nes_message_data_static, so that message 0xFFFC is definitely loaded if it
    // exists
    if (sNesMessageEntryTablePtr == NULL) {
        sNesMessageEntryTablePtr =
            OTRMessage_LoadTable("text/nes_message_data_static/ntsc_nes_message_data_static", false);
    }

    if (sStaffMessageEntryTablePtr == NULL) {
        auto file2 =
            std::static_pointer_cast<SOH::Text>(Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(
                "text/staff_message_data_static/staff_message_data_static"));
        // SoH-3DS: with no archives mounted (e.g. oot.o2r unresolved) this
        // resource is null and the deref below was the first boot crash a
        // broken asset path produced - a mystery data abort instead of a
        // diagnosable failure. Fail loudly and skip staff credits text.
        if (file2 == nullptr) {
            SPDLOG_ERROR("OTRMessage_Init: staff_message_data_static missing - are the .o2r archives mounted?");
            fprintf(stderr, "soh-3ds msg: staff message table missing; skipping (no archives?)\n");
            return;
        }
        // OTRTODO: Should not be malloc'ing here. It's fine for now since we check that the message table is already
        // null.
        sStaffMessageEntryTablePtr = (MessageTableEntry*)malloc(sizeof(MessageTableEntry) * file2->messages.size());

        for (size_t i = 0; i < file2->messages.size(); i++) {
            SetMessageEntry(sStaffMessageEntryTablePtr[i], file2->messages[i]);
        }
        OTRMessage_LoadCustom("override/text/staff_message_data_static/*", sStaffMessageEntryTablePtr,
                              file2->messages.size());

        // Assert staff credits start at the first credits ID
        assert(sStaffMessageEntryTablePtr[0].textId == 0x0500);
    }
}
