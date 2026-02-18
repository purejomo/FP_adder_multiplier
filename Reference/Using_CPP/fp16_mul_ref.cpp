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
    bool underflow;
};

// ----------------------------------------------------------------------------
// Bit-True Function: Hardware Logic Emulation (Multiplier)
// ----------------------------------------------------------------------------
// This mimics the Verilog behavior for FP16 Multiplication
BitTrueResult fp16_mul_bittrue(fp16_t n1, fp16_t n2) {
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
    bool n1_is_zero = (e1 == 0) && (f1 == 0);
    bool n2_is_zero = (e2 == 0) && (f2 == 0);

    // Compute Result Sign
    uint16_t s_res = s1 ^ s2;

    // NaN Handling
    if (n1_is_nan || n2_is_nan) {
        ret.res = 0x7FFF; ret.nan = true; return ret;
    }
    // Inf * 0 = NaN
    if ((n1_is_inf && n2_is_zero) || (n2_is_inf && n1_is_zero)) {
        ret.res = 0x7FFF; ret.nan = true; return ret;
    }
    // Infinity Handling
    if (n1_is_inf || n2_is_inf) {
        ret.overflow = true;
        ret.res = (s_res << 15) | 0x7C00;
        return ret;
    }
    // Zero Handling
    if (n1_is_zero || n2_is_zero) {
        ret.zero = true;
        ret.res = (s_res << 15); // Signed Zero
        return ret;
    }

    // 3. Extract Mantissa & Exponent (Handling Denormals)
    // Note: This HW model assumes simplified denormal handling (flush to zero or treat as 0.xxx)
    // For full IEEE 754, we need to handle denormals precisely.
    // Here we treat denormals as having exponent 1 but mantissa 0.xxx (without hidden bit)
    
    int32_t exp1 = (e1 == 0) ? 1 : e1; 
    int32_t exp2 = (e2 == 0) ? 1 : e2;
    
    uint32_t mant1 = (e1 == 0) ? f1 : (f1 | 1024); 
    uint32_t mant2 = (e2 == 0) ? f2 : (f2 | 1024);

    // 4. Exponent Calculation
    // Bias is 15. E_res = E1 + E2 - Bias
    int32_t exp_res = exp1 + exp2 - 15;

    // 5. Mantissa Multiplication
    // 11 bits * 11 bits = 22 bits (max)
    uint32_t mant_mult = mant1 * mant2;

    // 6. Normalization
    // Result of 1.x * 1.y is in [1, 4)
    // If result >= 2.0 (bit 21 is 1), shift right and increment exponent
    // range of mant_mult: 
    // Min (1.0 * 1.0): 1024 * 1024 = 1048576 (0x100000) - bit 20 is 1
    // Max (near 2.0 * 2.0): ~2047 * ~2047 = ~4190209 (0x3FF001) - bit 21 might be 1
    
    if (mant_mult & 0x200000) { // Bit 21 is set (Result >= 2.0)
        // Normalize: Right Shift 1
        mant_mult >>= 1;
        exp_res++;
    }
    // Else: Bit 20 should be set for normalized numbers.

    // 7. Handling Exponent Overflow/Underflow
    if (exp_res >= 31) { // Overflow
        ret.overflow = true;
        ret.res = (s_res << 15) | 0x7C00;
    } 
    else if (exp_res <= 0) { // Underflow to Zero/Denormal
        // For simplicity, flush negative exponents to zero or minimal denormal
        // A real HW might shift right to make it denormal.
        
        if (exp_res < -10) { // Too small
             ret.underflow = true;
             ret.zero = true;
             ret.res = (s_res << 15);
        } else {
             // Denormalize
             // Shift amount = 1 - exp_res
             int shift = 1 - exp_res;
             mant_mult >>= shift;
             exp_res = 0;
             
             if (mant_mult == 0) ret.zero = true;
             
             // Pack Denormal
             // Remove hidden bit? No, denormal doesn't have hidden bit.
             // But our mant_mult includes the integer part. 
             // We need to take the top 10 bits of fractional part.
             // Wait, for Denormal, E=0, Fraction is the bits.
             
             // Current mant_mult is scaled such that bit 20 is the unit.
             // We need to align it to bit 10 for storage.
             // Normal: bit 20 is hidden, 19-10 are stored.
             // Denormal: bit 20 is 0. 19-10...
             
             ret.res = (s_res << 15) | (exp_res << 10) | ((mant_mult >> 10) & 0x3FF);
        }
    } 
    else { // Normal result
        // Pack: Sign | Exp | Mantissa
        // mant_mult: bit 20 is hidden bit (1). Bits 19-10 are the top 10 fraction bits.
        // We drop bit 20.
        ret.res = (s_res << 15) | (exp_res << 10) | ((mant_mult >> 10) & 0x3FF);
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
        {0x3C00, 0x3C00}, // 1.0 * 1.0 = 1.0
        {0x3C00, 0x4000}, // 1.0 * 2.0 = 2.0
        {0x3C00, 0x4200}, // 1.0 * 3.0 = 3.0
        {0x4000, 0x3800}, // 2.0 * 0.5 = 1.0
        {0xC000, 0x4000}, // -2.0 * 2.0 = -4.0
        {0x0000, 0x3C00}, // 0 * 1.0 = 0
        {0x8000, 0x4000}, // -0 * 2.0 = -0
        {0x7C00, 0x3C00}, // Inf * 1.0 = Inf
        {0x7C00, 0x8000}, // Inf * -0 = NaN (Invalid)
        {0x7FFF, 0x3C00}, // NaN * 1.0 = NaN
        {0x3C00, 0x0400}  // 1.0 * Smallest Normal
    };

    // 2. Random Test Cases (Add 20 random Inputs)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFF);
    
    for (int i = 0; i < 20; ++i) {
        tests.push_back({(fp16_t)dis(gen), (fp16_t)dis(gen)});
    }

    std::cout << "--------------------------------------------------------------------------------------------------\n";
    std::cout << " FP16 Multiplier Verification: Bit-True (HW) vs TLM (Float)\n";
    std::cout << "--------------------------------------------------------------------------------------------------\n";
    std::cout << "  Input A  |  Input B  || HW Res  | TLM Res | Match? | OF | Z | NaN| Note\n";
    std::cout << "--------------------------------------------------------------------------------------------------\n";

    int mismatch_count = 0;

    for (const auto& t : tests) {
        // Run HW Model
        BitTrueResult hw = fp16_mul_bittrue(t.first, t.second);
        
        // Run TLM Model (ideal float multiplication)
        float fa = fp16_to_float(t.first);
        float fb = fp16_to_float(t.second);
        float fmult = fa * fb;
        fp16_t tlm_res = float_to_fp16(fmult); // Convert back for comparison

        // Compare
        bool match = (hw.res == tlm_res);
        // Exception: NaNs never equal
        if (std::isnan(fmult) && hw.nan) match = true;
        
        std::string note = "";
        if (!match) {
            mismatch_count++;
            note = "Mismatch";
        }

        std::cout << "  0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << t.first
                  << "   |  0x" << std::setw(4) << t.second 
                  << "   || 0x" << std::setw(4) << hw.res
                  << "  | 0x" << std::setw(4) << tlm_res
                  << "  |   " << (match ? "O" : "X")
                  << "    | " << hw.overflow 
                  << "  | " << hw.zero 
                  << " | "  << hw.nan 
                  << "  | " << note << "\n";
    }
    
    std::cout << "--------------------------------------------------------------------------------------------------\n";
    std::cout << "Total Mismatches: " << std::dec << mismatch_count << "\n";

    return 0;
}
