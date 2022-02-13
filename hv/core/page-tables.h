#pragma once

#include <ia32.hpp>

namespace hv {

struct host_page_tables {
  // array of PML4 entries that point to a PDPT
  alignas(0x1000) pml4e_64 pml4[512];
};

// initialize the host page tables
void prepare_host_page_tables(host_page_tables& pt);

} // namespace hv

