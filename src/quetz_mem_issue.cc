// Copyright 2009-2026 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2026, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "quetz_mem_issue.h"
#include "quetz_memory_span.h"
#include <limits>

#include <algorithm>
#include <cstring>
#include <inttypes.h>
#include <vector>

using namespace SST;
using namespace SST::Quetz;
using namespace SST::Interfaces;

MemRequestEmitter::MemRequestEmitter(
        ComponentExtension* comp,
        SST::Output*      out,
        uint32_t          coreID,
        TimeConverter     tc,
        uint64_t          cacheLineSize,
        uint32_t          checkAddresses,
        QuetzCoreStats&   stats)
    : comp_(comp),
      mem_link_(nullptr),
      mmio_link_(nullptr),
      output_(out),
      core_id_(coreID),
      tc_(tc),
      cache_line_size_(cacheLineSize),
      check_addresses_(checkAddresses),
      stats_(stats),
      pending_count_(0)
{}

SST::Interfaces::StandardMem* MemRequestEmitter::linkFor(IssuePath path) const {
    return (path == IssuePath::MMIO) ? mmio_link_ : mem_link_;
}

uint32_t MemRequestEmitter::slotsNeeded(uint64_t vaddr, uint32_t size,
                                        IssuePath path) const {
    if (path == IssuePath::MMIO)
        return 1;
    return memorySlots(vaddr, size, cache_line_size_);
}

void MemRequestEmitter::issueRead(uint64_t vaddr, uint32_t size, uint64_t ,
                                  IssuePath path) {
    uint32_t offset = 0;
    issueReadWindow(vaddr, size, 0, path, offset, UINT32_MAX);
}

uint32_t MemRequestEmitter::issueReadWindow(uint64_t vaddr, uint32_t size, uint64_t,
                                           IssuePath path, uint32_t& offset,
                                           uint32_t budget) {
    if (size == 0 || budget == 0) return 0;
    if (!memorySpanValid(vaddr, size))
        output_->fatal(CALL_INFO, -1, "Memory read wraps the address space.\n");
    output_->verbose(CALL_INFO, 8, 0,
        "QuetzCore %" PRIu32 " READ  vaddr=0x%016" PRIx64 " size=%" PRIu32
        " path=%s\n",
        core_id_, vaddr, size,
        (path == IssuePath::MMIO) ? "mmio" : "cached");

    if (path != IssuePath::MMIO && offset == 0)
        stats_.read_req_sizes->addData(size);

    StandardMem* link = linkFor(path);
    if (!link) {
        output_->fatal(CALL_INFO, -1,
            "QuetzCore %" PRIu32 ": %s read to 0x%016" PRIx64 " but %s is not connected.\n",
            core_id_,
            (path == IssuePath::MMIO) ? "MMIO" : "cached",
            vaddr,
            (path == IssuePath::MMIO) ? "mmio_link" : "cache_link");
    }

    if (path == IssuePath::MMIO) {
        auto* req = new StandardMem::Read(vaddr, size, 0, vaddr);
        pending_txns_[req->getID()] = { req, comp_->getCurrentSimTime(tc_), true };
        pending_count_++;
        stats_.mmio_read_reqs->addData(1);
        offset = size;
        link->send(req);
        return 1;
    }

    uint64_t addr      = vaddr + offset;
    uint32_t remaining = size - offset;
    uint32_t parts     = 0;

    while (remaining > 0 && parts < budget) {
        uint32_t chunk = static_cast<uint32_t>(
            memoryChunkSize(addr, remaining, cache_line_size_, remaining));
        if (offset != 0) stats_.split_reads->addData(1);

        auto* req = new StandardMem::Read(addr, chunk, 0, addr);
        pending_txns_[req->getID()] = { req, comp_->getCurrentSimTime(tc_), false };
        pending_count_++;
        stats_.read_reqs->addData(1);
        link->send(req);

        addr      += chunk;
        offset    += chunk;
        remaining -= chunk;
        parts++;
    }

    if (check_addresses_ && size > (uint32_t)cache_line_size_)
        output_->verbose(CALL_INFO, 1, 0,
            "QuetzCore %" PRIu32 " READ vaddr=0x%016" PRIx64 " size=%" PRIu32
            " exceeds cache line size %" PRIu64 " (issued %" PRIu32 " sub-requests)\n",
            core_id_, vaddr, size, cache_line_size_, parts);
    return parts;
}

void MemRequestEmitter::issueWrite(uint64_t vaddr, uint32_t size, uint64_t ,
                                   const uint8_t* raw_data, IssuePath path) {
    uint32_t offset = 0;
    issueWriteWindow(vaddr, size, 0, raw_data, path, offset, UINT32_MAX);
}

uint32_t MemRequestEmitter::issueWriteWindow(uint64_t vaddr, uint32_t size, uint64_t,
                                            const uint8_t* raw_data, IssuePath path,
                                            uint32_t& offset, uint32_t budget) {
    if (size == 0 || budget == 0) return 0;
    if (!memorySpanValid(vaddr, std::min<uint32_t>(size, sizeof(QuetzCommand::data))))
        output_->fatal(CALL_INFO, -1, "Memory write wraps the address space.\n");
    output_->verbose(CALL_INFO, 8, 0,
        "QuetzCore %" PRIu32 " WRITE vaddr=0x%016" PRIx64 " size=%" PRIu32
        " path=%s\n",
        core_id_, vaddr, size,
        (path == IssuePath::MMIO) ? "mmio" : "cached");

    if (path != IssuePath::MMIO && offset == 0)
        stats_.write_req_sizes->addData(size);

    StandardMem* link = linkFor(path);
    if (!link) {
        output_->fatal(CALL_INFO, -1,
            "QuetzCore %" PRIu32 ": %s write to 0x%016" PRIx64 " but %s is not connected.\n",
            core_id_,
            (path == IssuePath::MMIO) ? "MMIO" : "cached",
            vaddr,
            (path == IssuePath::MMIO) ? "mmio_link" : "cache_link");
    }

    static constexpr uint32_t kDataCap = (uint32_t)sizeof(QuetzCommand::data);

    if (path == IssuePath::MMIO) {
        uint32_t issue_size = size;
        if (size > kDataCap) {
            stats_.mmio_truncated_writes->addData(1);
            issue_size = kDataCap;
            output_->verbose(CALL_INFO, 1, 0,
                "QuetzCore %" PRIu32 " MMIO WRITE size=%" PRIu32
                " exceeds plugin data cap %" PRIu32 " — truncating.\n",
                core_id_, size, kDataCap);
        }

        std::vector<uint8_t> data(issue_size, 0);
        if (raw_data && issue_size > 0)
            memcpy(data.data(), raw_data, issue_size);

        auto* req = new StandardMem::Write(vaddr, issue_size, data, false, 0, vaddr);
        pending_txns_[req->getID()] = { req, comp_->getCurrentSimTime(tc_), true };
        pending_count_++;
        stats_.mmio_write_reqs->addData(1);
        offset = size;
        link->send(req);
        return 1;
    }

    // The plugin can only carry kDataCap bytes of store payload in a
    // QuetzCommand, so bytes beyond that were never captured. Cap the cached
    // write to the bytes we actually have rather than splitting the access and
    // zero-filling the trailing cache lines — fabricated zeros would silently
    // corrupt guest memory (and any balar packet staged in this range).
    uint32_t issue_size = size;
    if (size > kDataCap) {
        if (offset == 0) stats_.cached_truncated_writes->addData(1);
        issue_size = kDataCap;
        output_->verbose(CALL_INFO, 1, 0,
            "QuetzCore %" PRIu32 " cached WRITE vaddr=0x%016" PRIx64 " size=%"
            PRIu32 " exceeds plugin data cap %" PRIu32 " — truncating to avoid "
            "writing fabricated data.\n",
            core_id_, vaddr, size, kDataCap);
    }

    uint64_t addr        = vaddr + offset;
    uint32_t remaining   = issue_size - offset;
    uint32_t data_offset = offset;
    uint32_t parts       = 0;

    while (remaining > 0 && parts < budget) {
        uint32_t chunk = static_cast<uint32_t>(
            memoryChunkSize(addr, remaining, cache_line_size_, remaining));
        if (offset != 0) stats_.split_writes->addData(1);

        std::vector<uint8_t> data(chunk, 0);
        if (raw_data && data_offset < kDataCap) {
            uint32_t avail  = kDataCap - data_offset;
            uint32_t copy_n = (chunk < avail) ? chunk : avail;
            memcpy(data.data(), raw_data + data_offset, copy_n);
        }

        auto* req = new StandardMem::Write(addr, chunk, data, false, 0, addr);
        pending_txns_[req->getID()] = { req, comp_->getCurrentSimTime(tc_), false };
        pending_count_++;
        stats_.write_reqs->addData(1);
        link->send(req);

        addr        += chunk;
        data_offset += chunk;
        offset      += chunk;
        remaining   -= chunk;
        parts++;
    }

    // Uncaptured bytes are deliberately omitted, but the original command is
    // complete once every captured byte has been sent.
    if (offset == issue_size) offset = size;

    if (check_addresses_ && size > (uint32_t)cache_line_size_)
        output_->verbose(CALL_INFO, 1, 0,
            "QuetzCore %" PRIu32 " WRITE vaddr=0x%016" PRIx64 " size=%" PRIu32
            " exceeds cache line size %" PRIu64 " (issued %" PRIu32 " sub-requests)\n",
            core_id_, vaddr, size, cache_line_size_, parts);
    return parts;
}

bool MemRequestEmitter::handleResponse(StandardMem::Request* resp,
                                       uint64_t& latency_out,
                                       bool& was_read_out,
                                       bool& was_mmio_out) {
    auto it = pending_txns_.find(resp->getID());
    if (it == pending_txns_.end()) {
        output_->verbose(CALL_INFO, 4, 0,
            "QuetzCore %" PRIu32 ": ignoring untracked response id %" PRIu64 "\n",
            core_id_, (uint64_t)resp->getID());
        delete resp;
        was_mmio_out = false;
        return false;
    }

    uint64_t issue = it->second.issue_cycle;
    uint64_t now   = comp_->getCurrentSimTime(tc_);
    latency_out    = (now >= issue) ? (now - issue) : 0;
    was_read_out   = (dynamic_cast<StandardMem::ReadResp*>(resp) != nullptr);
    was_mmio_out   = it->second.is_mmio;

    pending_txns_.erase(it);
    pending_count_--;
    delete resp;
    return true;
}
