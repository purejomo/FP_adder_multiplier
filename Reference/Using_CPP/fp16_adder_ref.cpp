#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cmath>
#include <vector>
#include <bitset>
#include <cstring>
#include <random>

// ----------------------------------------------------------------------------
// FP16 Types & Helpers
// ----------------------------------------------------------------------------
typedef uint16_t fp16_t;

// Union for bit manipulation of float (32-bit)
union FloatBits {
    float f;
    uint32_t i;
};

// Convert FP16 to Float32 (Standard IEEE 754 logic)
float fp16_to_float(fp16_t h) {
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t frac = h & 0x3FF;

    if (exp == 0) {
        if (frac == 0) { // Signed Zero
            float res = 0.0f;
            uint32_t bits;
            std::memcpy(&bits, &res, 4);
            bits |= (sign << 31);
            std::memcpy(&res, &bits, 4);
            return res;
        } 
        else { // Subnormal
            return std::ldexp((float)frac, -24) * (sign ? -1.0f : 1.0f);
        }
    } 
    else if (exp == 31) {
        if (frac == 0) return sign ? -INFINITY : INFINITY;
        else return NAN; // NaN
    } 
    else { // Normal
        return std::ldexp(1.0f + (float)frac / 1024.0f, exp - 15) * (sign ? -1.0f : 1.0f);
    }
}

// Convert Float32 to FP16 (Truncation/Round to Zero style for TLM comparison)
// This is a "Golden Reference" for the mathematical value.
fp16_t float_to_fp16(float f) {
    FloatBits fb;
    fb.f = f;
    uint32_t sign = (fb.i >> 31) & 0x1;
    int32_t exp = ((fb.i >> 23) & 0xFF) - 127;
    uint32_t mant = fb.i & 0x7FFFFF;

    if (std::isnan(f)) return 0x7FFF; // Canonical NaN
    if (std::isinf(f)) return (sign << 15) | 0x7C00; 

    if (f == 0.0f) return (sign << 15); // Zero

    // Normalized to FP16 range
    int32_t new_exp = exp + 15;
    
    if (new_exp <= 0) { // Denormal or Underflow
        // Simplified: Flush to zero or handle denormal
        // For TLM comparison, let's just use simple conversion
        if (new_exp < -10) return (sign << 15); // Too small
        
        // Denormalize
        mant = (mant | 0x800000) >> (1 - new_exp);
        return (sign << 15) | (mant >> 13);
        
    } else if (new_exp >= 31) { // Overflow
        return (sign << 15) | 0x7C00;
    } else {
        return (sign << 15) | (new_exp << 10) | (mant >> 13);
    }
}

// ----------------------------------------------------------------------------
// Result Structures
// ----------------------------------------------------------------------------
struct BitTrueResult {
    fp16_t res;
    bool overflow;
    bool zero;
    bool nan;
    bool precision_lost;
};

// ----------------------------------------------------------------------------
// Bit-True Function: Hardware Logic Emulation (Truncation based)
// ----------------------------------------------------------------------------
// This mimics the Verilog behavior (Truncation / Round towards Zero)
BitTrueResult fp16_add_bittrue(fp16_t n1, fp16_t n2) {
    BitTrueResult ret = {0, false, false, false, false};

    // 1. Decode inputs
    uint16_t s1 = (n1 >> 15) & 1;
    uint16_t e1 = (n1 >> 10) & 0x1F;
    uint16_t f1 = n1 & 0x3FF;

    uint16_t s2 = (n2 >> 15) & 1;
    uint16_t e2 = (n2 >> 10) & 0x1F;
    uint16_t f2 = n2 & 0x3FF;

    // 2. Check Special Values
    bool n1_is_inf = (e1 == 31) && (f1 == 0);
    bool n2_is_inf = (e2 == 31) && (f2 == 0);
    bool n1_is_nan = (e1 == 31) && (f1 != 0);
    bool n2_is_nan = (e2 == 31) && (f2 != 0);

    // NaN Handling
    if (n1_is_nan || n2_is_nan || (n1_is_inf && n2_is_inf && (s1 != s2))) {
        ret.res = 0x7FFF; ret.nan = true; return ret;
    }

    // Infinity Handling
    if (n1_is_inf || n2_is_inf) {
        ret.overflow = true;
        if (n1_is_inf) ret.res = n1; else ret.res = n2;
        return ret;
    }

    // 3. Align (Big/Small) - Treat denormal exp as 1 for diff calc
    int32_t exp1 = (e1 == 0) ? 1 : e1; 
    int32_t exp2 = (e2 == 0) ? 1 : e2;
    
    // Add hidden bit
    uint32_t mant1 = (e1 == 0) ? f1 : (f1 | 1024); 
    uint32_t mant2 = (e2 == 0) ? f2 : (f2 | 1024);

    bool swap = false;
    if (exp1 < exp2) swap = true;
    else if (exp1 == exp2 && mant1 < mant2) swap = true;

    uint16_t sign_big = swap ? s2 : s1;
    int32_t  exp_big  = swap ? exp2 : exp1;
    uint32_t mant_big = swap ? mant2 : mant1;

    uint16_t sign_sml = swap ? s1 : s2;
    int32_t  exp_sml  = swap ? exp1 : exp2;
    uint32_t mant_sml = swap ? mant1 : mant2;

    int32_t exp_diff = exp_big - exp_sml;

    // 4. Shift Small Mantissa
    uint32_t mant_sml_shifted = 0;
    uint32_t bits_lost = 0; // "Precision Lost" tracking

    if (exp_diff >= 11 + 2) { 
        mant_sml_shifted = 0;
        bits_lost = (mant_sml != 0); 
    } else {
        mant_sml_shifted = mant_sml >> exp_diff;
        uint32_t mask = (1 << exp_diff) - 1;
        bits_lost = (mant_sml & mask);
    }
    
    // 5. Add/Sub
    int32_t mant_res_signed;
    if (sign_big == sign_sml) {
        mant_res_signed = mant_big + mant_sml_shifted;
    } else {
        mant_res_signed = mant_big - mant_sml_shifted;
    }

    // 6. Normalize
    int32_t final_exp = exp_big;
    uint32_t final_mant = mant_res_signed;

    if (final_mant == 0) {
        ret.res = 0; 
        if (sign_big == sign_sml && sign_big == 1) ret.res = 0x8000; // -0
        ret.zero = true;
        if (bits_lost) ret.precision_lost = true; 
        return ret;
    }

    // Renormalize
    if (final_mant >= 2048) { // Overflow
        if (final_mant & 1) bits_lost = 1; // Accumulate lost
        final_mant >>= 1;
        final_exp++;
    } else { // Normalize (for subtraction)
        while (final_mant < 1024 && final_exp > 1) { 
             final_mant <<= 1;
             final_exp--;
        }
        if (final_mant < 1024 && final_exp == 1) final_exp = 0; // Denormal
    }

    // 7. Precision Lost Flag
    if (bits_lost) ret.precision_lost = true;

    // 8. Pack Result
    if (final_exp >= 31) {
        ret.overflow = true;
        ret.res = (sign_big << 15) | 0x7C00; // Inf
    } else {
        ret.res = (sign_big << 15) | (final_exp << 10) | (final_mant & 0x3FF);
    }
    
    if ((ret.res & 0x7FFF) == 0) ret.zero = true;

    return ret;
}


// ----------------------------------------------------------------------------
// Main: Verification
// ----------------------------------------------------------------------------
int main() {
    // 1. Fixed Test Cases
    std::vector<std::pair<fp16_t, fp16_t>> tests = {
        {0xC0B0, 0x1CC0}, // Bug Case 1
        {0x00E0, 0x5060}, // Normal + Normal
        {0x3C00, 0x3C00}, // 1.0 + 1.0
        {0x3C00, 0xBC00}, // 1.0 - 1.0
        {0x7C00, 0x3C00}, // Inf + 1.0
        {0x7FFF, 0x3C00}, // NaN + 1.0
        {0x5140, 0x1CC0}, // Precision Loss
        // Additional Edge Cases
        {0x3C00, 0x3800}, // 1.0 + 0.5
        {0x3C00, 0x0400}, // 1.0 + Smallest Normal
        {0x0400, 0x03FF}  // Smallest Normal + Largest Denormal
    };

    // 2. Random Test Cases (Add 20 random Inputs)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);
    
    for (int i = 0; i < 20; ++i) {
        tests.push_back({(fp16_t)dis(gen), (fp16_t)dis(gen)});
    }

    std::cout << "--------------------------------------------------------------------------------------------------\n";
    std::cout << " FP16 Adder Verification: Bit-True (HW) vs TLM (Float)\n";
    std::cout << "--------------------------------------------------------------------------------------------------\n";
    std::cout << "  Input A  |  Input B  || HW Res  | TLM Res | Match? | OF | Z | NaN| PL | Note\n";
    std::cout << "--------------------------------------------------------------------------------------------------\n";

    int mismatch_count = 0;

    for (const auto& t : tests) {
        // Run HW Model
        BitTrueResult hw = fp16_add_bittrue(t.first, t.second);
        
        // Run TLM Model (ideal float addition)
        float fa = fp16_to_float(t.first);
        float fb = fp16_to_float(t.second);
        float fsum = fa + fb;
        fp16_t tlm_res = float_to_fp16(fsum); // Convert back for comparison

        // Compare
        bool match = (hw.res == tlm_res);
        // Exception: NaNs never equal, check both are NaN
        if (std::isnan(fsum) && hw.nan) match = true;
        
        // Precision Loss Check Logic
        // In HW design, Truncation is simpler but Round-to-Nearest is standard.
        // HW truncation usually results in slightly smaller magnitude than TLM (Round to Nearest).
        
        std::string note = "";
        if (!match) {
            mismatch_count++;
            note = "Mismatch (Rounding Diff?)";
        }
        if (hw.precision_lost) {
             if (note.empty()) note = "Precision Lost";
             else note += ", P-Lost";
        }

        std::cout << "  0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << t.first
                  << "   |  0x" << std::setw(4) << t.second 
                  << "   || 0x" << std::setw(4) << hw.res
                  << "  | 0x" << std::setw(4) << tlm_res
                  << "  |   " << (match ? "O" : "X")
                  << "    | " << hw.overflow 
                  << "  | " << hw.zero 
                  << " | "  << hw.nan 
                  << "  | "  << hw.precision_lost
                  << "  | " << note << "\n";
    }
    
    std::cout << "--------------------------------------------------------------------------------------------------\n";
    std::cout << "Total Mismatches: " << std::dec << mismatch_count << " (differences between HW Truncation & TLM Rounding)\n";

    return 0;
}
