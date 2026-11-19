#pragma once

#include "MaaFramework/MaaAPI.h"

#include <string>

// MaaStringBuffer 的 RAII 包装，析构自动销毁。
class MaaBuffer
{
public:
    MaaBuffer() : buffer_(MaaStringBufferCreate()) {}
    ~MaaBuffer() { MaaStringBufferDestroy(buffer_); }

    MaaBuffer(const MaaBuffer&) = delete;
    MaaBuffer& operator=(const MaaBuffer&) = delete;

    MaaStringBuffer* get() const { return buffer_; }

    std::string str() const
    {
        return std::string(MaaStringBufferGet(buffer_), MaaStringBufferSize(buffer_));
    }

private:
    MaaStringBuffer* buffer_;
};
