#pragma once

#include "setup_core.h"

namespace Soh3dsSetup {

bool IsMetadataVariant(const std::string& variant);
// Streaming, bounded gzip decoding. Destination is replaced only after CRC and
// stream completion checks; failed attempts remove their .part file.
bool InflateGzipFile(const std::string& source, const std::string& destination, size_t maximumBytes,
                     Progress progress, void* user, std::string& error);
// Destination must not exist. Only config.yml and the selected variant's YAML
// files are accepted. Failure/cancellation removes the newly created tree.
bool UnpackMetadata(const std::string& source, const std::string& destination, const std::string& variant,
                    Progress progress, void* user, std::string& error);
// Only call on the reserved setup metadata directory. Does not follow symlinks.
bool CleanupMetadata(const std::string& directory, std::string& error);

} // namespace Soh3dsSetup
