#ifndef _SPIFFS_IMAGE_H_
#define _SPIFFS_IMAGE_H_

#include <cstdint>
#include <cstddef>

/// SPIFFS-based image storage for custom idle screen images.
///
/// Image format: raw 1-bit bitmap, MSB first, row-major.
/// Dimensions: RLCD_WIDTH x RLCD_HEIGHT (400x300 = 15000 bytes).
/// Supports 4 slots: /spiffs/slot0.raw .. /spiffs/slot3.raw
class SpiffsImage {
public:
    SpiffsImage() = default;
    ~SpiffsImage() = default;

    static constexpr int MAX_SLOTS = 4;

    bool Init();

    bool SaveImage(const uint8_t* data, size_t len, int slot);
    size_t LoadImage(uint8_t* buf, size_t buf_size, int slot);
    bool HasCustomImage(int slot);
    void DeleteImage(int slot);
    bool HasAnyImage();
    int GetSlotCount();

    static constexpr size_t IMAGE_SIZE = (400 * 300) / 8;

private:
    static constexpr const char* SPIFFS_BASE = "/spiffs";
    bool mounted_ = false;
};

#endif // _SPIFFS_IMAGE_H_
