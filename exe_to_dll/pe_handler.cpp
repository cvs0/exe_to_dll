#include "pe_handler.h"
#include "exports_block.h"

bool PeHandler::isDll()
{
    const IMAGE_FILE_HEADER* hdr = peconv::get_file_hdr(pe_ptr, v_size);
    if (!hdr) {
        return false;
    }
    return hdr->Characteristics & IMAGE_FILE_DLL;
}

bool PeHandler::isConvertable()
{
    return peconv::has_valid_relocation_table(pe_ptr, v_size);
}

bool PeHandler::setExe()
{
    IMAGE_FILE_HEADER* hdr = const_cast<IMAGE_FILE_HEADER*>(peconv::get_file_hdr(pe_ptr, v_size));
    if (!hdr) {
        return false;
    }

    hdr->Characteristics ^= IMAGE_FILE_DLL;
    return true;
}

bool PeHandler::exeToDllPatch()
{
    BYTE back_stub32[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00, // MOV EAX, 1
        0xC2, 0x0C, 0x00              // retn 0x0C
    };

    BYTE back_stub64[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00, // MOV EAX, 1
        0xC3                          // ret
    };

    BYTE* back_stub = back_stub32;
    size_t stub_size = sizeof(back_stub32);
    if (is64bit) {
        back_stub = back_stub64;
        stub_size = sizeof(back_stub64);
    }
    BYTE* ptr = nullptr;
    // find matching artifact:
    size_t sec_count = peconv::get_sections_count(pe_ptr, v_size);
    for (size_t i = 0; i < sec_count; i++) {
        PIMAGE_SECTION_HEADER section_hdr = peconv::get_section_hdr(pe_ptr, v_size, i);
        if (!section_hdr) continue;
        if (!(section_hdr->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        // we will be searching in the loaded, virtual image:
        DWORD sec_start = section_hdr->VirtualAddress;
        if (sec_start == 0) continue;

        DWORD sec_end = sec_start + section_hdr->SizeOfRawData;
        for (size_t i = sec_start; (i + stub_size) < sec_end; i++) {
            BYTE* _ptr = pe_ptr + sec_start + i;
            if (::memcmp(_ptr, back_stub, stub_size) == 0) {
                ptr = _ptr;
                std::cout << "Found stub pattern in the existing section!\n";
                break;
            }
        }
    }
    if (!ptr) {
        BYTE* _ptr = peconv::find_padding_cave(pe_ptr, v_size, stub_size, IMAGE_SCN_MEM_EXECUTE);
        if (!_ptr) {
            return false;
        }
        ::memmove(_ptr, back_stub, stub_size);
        ptr = _ptr;
        std::cout << "Saved stub in the section cave!\n";
    }
    const DWORD new_ep = DWORD(ptr - this->pe_ptr);
    return peconv::update_entry_point_rva(this->pe_ptr, new_ep);
}

bool PeHandler::savePe(const char* out_path)
{
    std::string path = out_path;
    std::string dllname = path.substr(path.find_last_of("/\\") + 1);

    ExportsBlock exp(this->ep, dllname.c_str(), "Start");
    if (!exp.appendToPE(pe_ptr)) {
        std::cerr << "[!] Failed to create an Export Directory!\n";
        return false;
    }
    size_t out_size = 0;
    ULONGLONG module_base = peconv::get_image_base(pe_ptr);

    BYTE* unmapped_module = peconv::pe_virtual_to_raw(pe_ptr,
        v_size,
        module_base,
        out_size);
    if (!unmapped_module) {
        return false;
    }
    bool is_ok = peconv::dump_to_file(out_path, unmapped_module, out_size);
    peconv::free_pe_buffer(unmapped_module, v_size);
    return is_ok;
}
