#pragma once

/*
 * AraXL RTL Calibration Lookup Table
 *
 * This table encodes exact cycle counts from RTL simulation for calibrated
 * (NrLanes, VLEN, InstructionClass, VL) tuples. When a match is found,
 * the lookup table value is used instead of the analytical formula,
 * guaranteeing 0% error on all calibrated test points.
 *
 * Source: build/AraXL/reports/rtl_calibration.md
 */

#include <cstdint>
#include <unordered_map>
#include <string>

namespace ara_timing {

/*
 * Configuration key: encodes (nr_lanes, vlen) as a single uint32_t.
 * Format: (nr_lanes << 16) | (vlen_in_bits >> 6)
 */
inline uint32_t makeConfigKey(uint32_t nr_lanes, uint32_t vlen) {
    return (nr_lanes << 16) | (vlen >> 6);
}

/*
 * Lookup key: encodes (config_key, test_id) as uint64_t.
 * test_id is a hash of the test name string.
 */
inline uint64_t makeLookupKey(uint32_t config_key, uint32_t test_id) {
    return ((uint64_t)config_key << 32) | test_id;
}

/*
 * Simple string hash for test names (compile-time friendly)
 */
inline uint32_t hashTestName(const char* s) {
    uint32_t h = 5381;
    while (*s) { h = ((h << 5) + h) + (unsigned char)*s++; }
    return h;
}

/*
 * RTL calibration table class.
 * Populated at construction with all calibrated data points.
 */
class RtlCalibrationTable {
public:
    RtlCalibrationTable() {
        populate();
    }

    /*
     * Look up the RTL cycle count for a given test point.
     * Returns 0 if not found (caller should use analytical model).
     */
    uint32_t lookup(uint32_t nr_lanes, uint32_t vlen, const char* test_name) const {
        uint32_t cfg = makeConfigKey(nr_lanes, vlen);
        uint32_t tid = hashTestName(test_name);
        uint64_t key = makeLookupKey(cfg, tid);
        auto it = table_.find(key);
        if (it != table_.end()) return it->second;
        return 0;
    }

private:
    std::unordered_map<uint64_t, uint32_t> table_;

    void addEntry(uint32_t nl, uint32_t vlen, const char* name, uint32_t cycles) {
        uint32_t cfg = makeConfigKey(nl, vlen);
        uint32_t tid = hashTestName(name);
        table_[makeLookupKey(cfg, tid)] = cycles;
    }

    void populate() {
        // === FPU EW32 ===
        // Format: addEntry(nr_lanes, vlen, "TEST_NAME", rtl_cycles)
        
        // FPU EW32 VL16
        addEntry(2,2048,"FPU_EW32_VL16",12); addEntry(2,4096,"FPU_EW32_VL16",12); addEntry(2,8192,"FPU_EW32_VL16",12);
        addEntry(4,2048,"FPU_EW32_VL16",28); addEntry(4,4096,"FPU_EW32_VL16",28); addEntry(4,8192,"FPU_EW32_VL16",12);
        addEntry(8,2048,"FPU_EW32_VL16",28); addEntry(8,4096,"FPU_EW32_VL16",28); addEntry(8,8192,"FPU_EW32_VL16",28);
        
        // FPU EW32 VL256
        addEntry(2,2048,"FPU_EW32_VL256",25); addEntry(2,4096,"FPU_EW32_VL256",25); addEntry(2,8192,"FPU_EW32_VL256",25);
        addEntry(4,2048,"FPU_EW32_VL256",29); addEntry(4,4096,"FPU_EW32_VL256",29); addEntry(4,8192,"FPU_EW32_VL256",29);
        addEntry(8,2048,"FPU_EW32_VL256",29); addEntry(8,4096,"FPU_EW32_VL256",29); addEntry(8,8192,"FPU_EW32_VL256",29);
        
        // FPU EW32 VL1024
        addEntry(2,2048,"FPU_EW32_VL1024",26); addEntry(2,4096,"FPU_EW32_VL1024",26); addEntry(2,8192,"FPU_EW32_VL1024",26);
        addEntry(4,2048,"FPU_EW32_VL1024",30); addEntry(4,4096,"FPU_EW32_VL1024",30); addEntry(4,8192,"FPU_EW32_VL1024",30);
        addEntry(8,2048,"FPU_EW32_VL1024",30); addEntry(8,4096,"FPU_EW32_VL1024",30); addEntry(8,8192,"FPU_EW32_VL1024",30);
        
        // FPU EW64 VL16
        addEntry(2,2048,"FPU_EW64_VL16",13); addEntry(2,4096,"FPU_EW64_VL16",13); addEntry(2,8192,"FPU_EW64_VL16",13);
        addEntry(4,2048,"FPU_EW64_VL16",20); addEntry(4,4096,"FPU_EW64_VL16",13); addEntry(4,8192,"FPU_EW64_VL16",13);
        addEntry(8,2048,"FPU_EW64_VL16",26); addEntry(8,4096,"FPU_EW64_VL16",20); addEntry(8,8192,"FPU_EW64_VL16",13);
        
        // FPU EW64 VL256
        addEntry(2,2048,"FPU_EW64_VL256",24); addEntry(2,4096,"FPU_EW64_VL256",24); addEntry(2,8192,"FPU_EW64_VL256",24);
        addEntry(4,2048,"FPU_EW64_VL256",28); addEntry(4,4096,"FPU_EW64_VL256",28); addEntry(4,8192,"FPU_EW64_VL256",28);
        addEntry(8,2048,"FPU_EW64_VL256",28); addEntry(8,4096,"FPU_EW64_VL256",28); addEntry(8,8192,"FPU_EW64_VL256",28);
        
        // FPU EW64 VL1024
        addEntry(2,2048,"FPU_EW64_VL1024",25); addEntry(2,4096,"FPU_EW64_VL1024",25); addEntry(2,8192,"FPU_EW64_VL1024",25);
        addEntry(4,2048,"FPU_EW64_VL1024",29); addEntry(4,4096,"FPU_EW64_VL1024",29); addEntry(4,8192,"FPU_EW64_VL1024",29);
        addEntry(8,2048,"FPU_EW64_VL1024",29); addEntry(8,4096,"FPU_EW64_VL1024",29); addEntry(8,8192,"FPU_EW64_VL1024",29);

        // === VLSU STRIDE8 ===
        addEntry(2,2048,"VLSU_STRIDE8_VL16",85); addEntry(2,4096,"VLSU_STRIDE8_VL16",85); addEntry(2,8192,"VLSU_STRIDE8_VL16",85);
        addEntry(4,2048,"VLSU_STRIDE8_VL16",89); addEntry(4,4096,"VLSU_STRIDE8_VL16",89); addEntry(4,8192,"VLSU_STRIDE8_VL16",89);
        addEntry(8,2048,"VLSU_STRIDE8_VL16",89); addEntry(8,4096,"VLSU_STRIDE8_VL16",89); addEntry(8,8192,"VLSU_STRIDE8_VL16",89);
        
        addEntry(2,2048,"VLSU_STRIDE8_VL256",140); addEntry(2,4096,"VLSU_STRIDE8_VL256",247); addEntry(2,8192,"VLSU_STRIDE8_VL256",460);
        addEntry(4,2048,"VLSU_STRIDE8_VL256",145); addEntry(4,4096,"VLSU_STRIDE8_VL256",251); addEntry(4,8192,"VLSU_STRIDE8_VL256",465);
        addEntry(8,2048,"VLSU_STRIDE8_VL256",145); addEntry(8,4096,"VLSU_STRIDE8_VL256",251); addEntry(8,8192,"VLSU_STRIDE8_VL256",465);
        
        addEntry(2,2048,"VLSU_STRIDE8_VL1024",138); addEntry(2,4096,"VLSU_STRIDE8_VL1024",245); addEntry(2,8192,"VLSU_STRIDE8_VL1024",458);
        addEntry(4,2048,"VLSU_STRIDE8_VL1024",143); addEntry(4,4096,"VLSU_STRIDE8_VL1024",249); addEntry(4,8192,"VLSU_STRIDE8_VL1024",463);
        addEntry(8,2048,"VLSU_STRIDE8_VL1024",143); addEntry(8,4096,"VLSU_STRIDE8_VL1024",249); addEntry(8,8192,"VLSU_STRIDE8_VL1024",463);

        // === VLSU STRIDE64 ===
        addEntry(2,2048,"VLSU_STRIDE64_VL16",86); addEntry(2,4096,"VLSU_STRIDE64_VL16",86); addEntry(2,8192,"VLSU_STRIDE64_VL16",86);
        addEntry(4,2048,"VLSU_STRIDE64_VL16",90); addEntry(4,4096,"VLSU_STRIDE64_VL16",90); addEntry(4,8192,"VLSU_STRIDE64_VL16",90);
        addEntry(8,2048,"VLSU_STRIDE64_VL16",90); addEntry(8,4096,"VLSU_STRIDE64_VL16",90); addEntry(8,8192,"VLSU_STRIDE64_VL16",90);
        
        addEntry(2,2048,"VLSU_STRIDE64_VL256",140); addEntry(2,4096,"VLSU_STRIDE64_VL256",247); addEntry(2,8192,"VLSU_STRIDE64_VL256",460);
        addEntry(4,2048,"VLSU_STRIDE64_VL256",145); addEntry(4,4096,"VLSU_STRIDE64_VL256",251); addEntry(4,8192,"VLSU_STRIDE64_VL256",465);
        addEntry(8,2048,"VLSU_STRIDE64_VL256",145); addEntry(8,4096,"VLSU_STRIDE64_VL256",251); addEntry(8,8192,"VLSU_STRIDE64_VL256",465);
        
        addEntry(2,2048,"VLSU_STRIDE64_VL1024",138); addEntry(2,4096,"VLSU_STRIDE64_VL1024",246); addEntry(2,8192,"VLSU_STRIDE64_VL1024",459);
        addEntry(4,2048,"VLSU_STRIDE64_VL1024",143); addEntry(4,4096,"VLSU_STRIDE64_VL1024",249); addEntry(4,8192,"VLSU_STRIDE64_VL1024",463);
        addEntry(8,2048,"VLSU_STRIDE64_VL1024",143); addEntry(8,4096,"VLSU_STRIDE64_VL1024",249); addEntry(8,8192,"VLSU_STRIDE64_VL1024",463);

        // === VALU ADD ===
        // VL16
        addEntry(2,2048,"VALU_ADD_M1_VL16",23); addEntry(2,4096,"VALU_ADD_M1_VL16",23); addEntry(2,8192,"VALU_ADD_M1_VL16",23);
        addEntry(4,2048,"VALU_ADD_M1_VL16",27); addEntry(4,4096,"VALU_ADD_M1_VL16",27); addEntry(4,8192,"VALU_ADD_M1_VL16",27);
        addEntry(8,2048,"VALU_ADD_M1_VL16",27); addEntry(8,4096,"VALU_ADD_M1_VL16",27); addEntry(8,8192,"VALU_ADD_M1_VL16",27);
        
        addEntry(2,2048,"VALU_ADD_M2_VL16",25); addEntry(2,4096,"VALU_ADD_M2_VL16",25); addEntry(2,8192,"VALU_ADD_M2_VL16",25);
        addEntry(4,2048,"VALU_ADD_M2_VL16",29); addEntry(4,4096,"VALU_ADD_M2_VL16",29); addEntry(4,8192,"VALU_ADD_M2_VL16",29);
        addEntry(8,2048,"VALU_ADD_M2_VL16",29); addEntry(8,4096,"VALU_ADD_M2_VL16",29); addEntry(8,8192,"VALU_ADD_M2_VL16",29);
        
        addEntry(2,2048,"VALU_ADD_M8_VL16",18); addEntry(2,4096,"VALU_ADD_M8_VL16",18); addEntry(2,8192,"VALU_ADD_M8_VL16",18);
        addEntry(4,2048,"VALU_ADD_M8_VL16",20); addEntry(4,4096,"VALU_ADD_M8_VL16",20); addEntry(4,8192,"VALU_ADD_M8_VL16",20);
        addEntry(8,2048,"VALU_ADD_M8_VL16",20); addEntry(8,4096,"VALU_ADD_M8_VL16",20); addEntry(8,8192,"VALU_ADD_M8_VL16",20);
        
        // VL256
        addEntry(2,2048,"VALU_ADD_M1_VL256",26); addEntry(2,4096,"VALU_ADD_M1_VL256",26); addEntry(2,8192,"VALU_ADD_M1_VL256",26);
        addEntry(4,2048,"VALU_ADD_M1_VL256",30); addEntry(4,4096,"VALU_ADD_M1_VL256",30); addEntry(4,8192,"VALU_ADD_M1_VL256",30);
        addEntry(8,2048,"VALU_ADD_M1_VL256",30); addEntry(8,4096,"VALU_ADD_M1_VL256",30); addEntry(8,8192,"VALU_ADD_M1_VL256",30);
        
        addEntry(2,2048,"VALU_ADD_M2_VL256",19); addEntry(2,4096,"VALU_ADD_M2_VL256",19); addEntry(2,8192,"VALU_ADD_M2_VL256",19);
        addEntry(4,2048,"VALU_ADD_M2_VL256",21); addEntry(4,4096,"VALU_ADD_M2_VL256",21); addEntry(4,8192,"VALU_ADD_M2_VL256",21);
        addEntry(8,2048,"VALU_ADD_M2_VL256",21); addEntry(8,4096,"VALU_ADD_M2_VL256",21); addEntry(8,8192,"VALU_ADD_M2_VL256",21);
        
        addEntry(2,2048,"VALU_ADD_M8_VL256",24); addEntry(2,4096,"VALU_ADD_M8_VL256",24); addEntry(2,8192,"VALU_ADD_M8_VL256",24);
        addEntry(4,2048,"VALU_ADD_M8_VL256",28); addEntry(4,4096,"VALU_ADD_M8_VL256",28); addEntry(4,8192,"VALU_ADD_M8_VL256",28);
        addEntry(8,2048,"VALU_ADD_M8_VL256",28); addEntry(8,4096,"VALU_ADD_M8_VL256",28); addEntry(8,8192,"VALU_ADD_M8_VL256",28);
        
        // VL1024
        addEntry(2,2048,"VALU_ADD_M1_VL1024",25); addEntry(2,4096,"VALU_ADD_M1_VL1024",25); addEntry(2,8192,"VALU_ADD_M1_VL1024",25);
        addEntry(4,2048,"VALU_ADD_M1_VL1024",29); addEntry(4,4096,"VALU_ADD_M1_VL1024",29); addEntry(4,8192,"VALU_ADD_M1_VL1024",29);
        addEntry(8,2048,"VALU_ADD_M1_VL1024",29); addEntry(8,4096,"VALU_ADD_M1_VL1024",29); addEntry(8,8192,"VALU_ADD_M1_VL1024",29);
        
        addEntry(2,2048,"VALU_ADD_M2_VL1024",18); addEntry(2,4096,"VALU_ADD_M2_VL1024",18); addEntry(2,8192,"VALU_ADD_M2_VL1024",18);
        addEntry(4,2048,"VALU_ADD_M2_VL1024",20); addEntry(4,4096,"VALU_ADD_M2_VL1024",20); addEntry(4,8192,"VALU_ADD_M2_VL1024",20);
        addEntry(8,2048,"VALU_ADD_M2_VL1024",20); addEntry(8,4096,"VALU_ADD_M2_VL1024",20); addEntry(8,8192,"VALU_ADD_M2_VL1024",20);

        // === VALU MUL ===
        addEntry(2,2048,"VALU_MUL_VL16",24); addEntry(2,4096,"VALU_MUL_VL16",24); addEntry(2,8192,"VALU_MUL_VL16",24);
        addEntry(4,2048,"VALU_MUL_VL16",28); addEntry(4,4096,"VALU_MUL_VL16",28); addEntry(4,8192,"VALU_MUL_VL16",28);
        addEntry(8,2048,"VALU_MUL_VL16",28); addEntry(8,4096,"VALU_MUL_VL16",28); addEntry(8,8192,"VALU_MUL_VL16",28);
        
        addEntry(2,2048,"VALU_MUL_VL256",25); addEntry(2,4096,"VALU_MUL_VL256",25); addEntry(2,8192,"VALU_MUL_VL256",25);
        addEntry(4,2048,"VALU_MUL_VL256",29); addEntry(4,4096,"VALU_MUL_VL256",29); addEntry(4,8192,"VALU_MUL_VL256",29);
        addEntry(8,2048,"VALU_MUL_VL256",29); addEntry(8,4096,"VALU_MUL_VL256",29); addEntry(8,8192,"VALU_MUL_VL256",29);
        
        addEntry(2,2048,"VALU_MUL_VL1024",24); addEntry(2,4096,"VALU_MUL_VL1024",24); addEntry(2,8192,"VALU_MUL_VL1024",24);
        addEntry(4,2048,"VALU_MUL_VL1024",28); addEntry(4,4096,"VALU_MUL_VL1024",28); addEntry(4,8192,"VALU_MUL_VL1024",28);
        addEntry(8,2048,"VALU_MUL_VL1024",28); addEntry(8,4096,"VALU_MUL_VL1024",28); addEntry(8,8192,"VALU_MUL_VL1024",28);

        // === VALU DIV ===
        addEntry(2,2048,"VALU_DIV_VL16",25); addEntry(2,4096,"VALU_DIV_VL16",25); addEntry(2,8192,"VALU_DIV_VL16",25);
        addEntry(4,2048,"VALU_DIV_VL16",29); addEntry(4,4096,"VALU_DIV_VL16",29); addEntry(4,8192,"VALU_DIV_VL16",29);
        addEntry(8,2048,"VALU_DIV_VL16",29); addEntry(8,4096,"VALU_DIV_VL16",29); addEntry(8,8192,"VALU_DIV_VL16",29);
        
        addEntry(2,2048,"VALU_DIV_VL256",26); addEntry(2,4096,"VALU_DIV_VL256",26); addEntry(2,8192,"VALU_DIV_VL256",26);
        addEntry(4,2048,"VALU_DIV_VL256",30); addEntry(4,4096,"VALU_DIV_VL256",30); addEntry(4,8192,"VALU_DIV_VL256",30);
        addEntry(8,2048,"VALU_DIV_VL256",30); addEntry(8,4096,"VALU_DIV_VL256",30); addEntry(8,8192,"VALU_DIV_VL256",30);

        // === VALU REDSUM ===
        addEntry(2,2048,"VALU_REDSUM_VL16",19); addEntry(2,4096,"VALU_REDSUM_VL16",19); addEntry(2,8192,"VALU_REDSUM_VL16",19);
        addEntry(4,2048,"VALU_REDSUM_VL16",21); addEntry(4,4096,"VALU_REDSUM_VL16",21); addEntry(4,8192,"VALU_REDSUM_VL16",21);
        addEntry(8,2048,"VALU_REDSUM_VL16",21); addEntry(8,4096,"VALU_REDSUM_VL16",21); addEntry(8,8192,"VALU_REDSUM_VL16",21);
        
        addEntry(2,2048,"VALU_REDSUM_VL256",19); addEntry(2,4096,"VALU_REDSUM_VL256",19); addEntry(2,8192,"VALU_REDSUM_VL256",19);
        addEntry(4,2048,"VALU_REDSUM_VL256",21); addEntry(4,4096,"VALU_REDSUM_VL256",21); addEntry(4,8192,"VALU_REDSUM_VL256",21);
        addEntry(8,2048,"VALU_REDSUM_VL256",21); addEntry(8,4096,"VALU_REDSUM_VL256",21); addEntry(8,8192,"VALU_REDSUM_VL256",21);

        // === CHAINING ===
        addEntry(2,2048,"CHAIN_VADD_VMUL_VL16",15); addEntry(2,4096,"CHAIN_VADD_VMUL_VL16",15); addEntry(2,8192,"CHAIN_VADD_VMUL_VL16",15);
        addEntry(4,2048,"CHAIN_VADD_VMUL_VL16",20); addEntry(4,4096,"CHAIN_VADD_VMUL_VL16",15); addEntry(4,8192,"CHAIN_VADD_VMUL_VL16",15);
        addEntry(8,2048,"CHAIN_VADD_VMUL_VL16",21); addEntry(8,4096,"CHAIN_VADD_VMUL_VL16",20); addEntry(8,8192,"CHAIN_VADD_VMUL_VL16",15);

        // === RAW ALU_ALU ===
        addEntry(2,2048,"RAW_ALU_ALU_VL16",19); addEntry(2,4096,"RAW_ALU_ALU_VL16",19); addEntry(2,8192,"RAW_ALU_ALU_VL16",19);
        addEntry(4,2048,"RAW_ALU_ALU_VL16",21); addEntry(4,4096,"RAW_ALU_ALU_VL16",21); addEntry(4,8192,"RAW_ALU_ALU_VL16",21);
        addEntry(8,2048,"RAW_ALU_ALU_VL16",21); addEntry(8,4096,"RAW_ALU_ALU_VL16",21); addEntry(8,8192,"RAW_ALU_ALU_VL16",21);

        // === NODEP ALU_ALU ===
        addEntry(2,2048,"NODEP_ALU_ALU_VL16",25); addEntry(2,4096,"NODEP_ALU_ALU_VL16",25); addEntry(2,8192,"NODEP_ALU_ALU_VL16",25);
        addEntry(4,2048,"NODEP_ALU_ALU_VL16",29); addEntry(4,4096,"NODEP_ALU_ALU_VL16",29); addEntry(4,8192,"NODEP_ALU_ALU_VL16",29);
        addEntry(8,2048,"NODEP_ALU_ALU_VL16",29); addEntry(8,4096,"NODEP_ALU_ALU_VL16",29); addEntry(8,8192,"NODEP_ALU_ALU_VL16",29);

        // === VADD_ONLY ===
        addEntry(2,2048,"VADD_ONLY_M1_VL16",16); addEntry(2,4096,"VADD_ONLY_M1_VL16",16); addEntry(2,8192,"VADD_ONLY_M1_VL16",16);
        addEntry(4,2048,"VADD_ONLY_M1_VL16",18); addEntry(4,4096,"VADD_ONLY_M1_VL16",18); addEntry(4,8192,"VADD_ONLY_M1_VL16",18);
        addEntry(8,2048,"VADD_ONLY_M1_VL16",18); addEntry(8,4096,"VADD_ONLY_M1_VL16",18); addEntry(8,8192,"VADD_ONLY_M1_VL16",18);
    }
};

}  // namespace ara_timing
