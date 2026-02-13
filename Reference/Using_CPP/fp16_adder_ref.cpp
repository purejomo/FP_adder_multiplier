#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cmath>
#include <vector>
#include <bitset>
#include <cstring> // for memcpy

// ----------------------------------------------------------------------------
// FP16 Type Definition
// ----------------------------------------------------------------------------
// IEEE 754 Half-precision (binary16):
// 1 bit: Sign
// 5 bits: Exponent (Bias = 15)
// 10 bits: Fraction (Mantissa)
// ----------------------------------------------------------------------------
typedef uint16_t fp16_t;

// ----------------------------------------------------------------------------
// Helper Function: Float32 <-> Float16
// ----------------------------------------------------------------------------
// We will use standard float (32-bit) arithmetic to emulate the "ideal" result,
// but for bit-exact verification including flags, we need to implement
// the addition logic manually as hardware does.
// However, first let's have a utility to inspect float values.
float fp16_to_float(fp16_t h) {
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t frac = h & 0x3FF;

    if (exp == 0) {
        if (frac == 0) {
            // Signed Zero
            float res = 0.0f;
            uint32_t bits;
            std::memcpy(&bits, &res, 4);
            bits |= (sign << 31);
            std::memcpy(&res, &bits, 4);
            return res;
        } else {
            // Subnormal
            return std::pow(2.0f, -14.0f) * (frac / 1024.0f) * (sign ? -1.0f : 1.0f);
        }
    } else if (exp == 31) {
        if (frac == 0) {
            // Infinity
            return sign ? -INFINITY : INFINITY;
        } else {
            // NaN
            return NAN;
        }
    } else {
        // Normal
        return std::pow(2.0f, (float)exp - 15.0f) * (1.0f + frac / 1024.0f) * (sign ? -1.0f : 1.0f);
    }
}

// ----------------------------------------------------------------------------
// Structure for Result and Flags
// ----------------------------------------------------------------------------
struct FP16Result {
    fp16_t res;
    bool overflow;
    bool zero;
    bool nan;
    bool precision_lost;
};

// ----------------------------------------------------------------------------
// Core Function: FP16 Addition (Hardware-like Model)
// ----------------------------------------------------------------------------
// This function mimics the behavior of the Verilog implementation line-by-line.
FP16Result fp16_add_model(fp16_t n1, fp16_t n2) {
    FP16Result ret = {0, false, false, false, false};

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
        ret.res = 0x7FFF; // Canonical NaN
        ret.nan = true;
        return ret; // Early exit for NaN
    }

    // Infinity Handling
    if (n1_is_inf || n2_is_inf) {
        ret.overflow = true; // In some implementations, Inf addition sets Overflow
        // If one is Inf, return that Inf. If both (same sign), return either.
        if (n1_is_inf) ret.res = n1;
        else ret.res = n2;
        return ret;
    }

    // 3. Align (Big/Small)
    // Add logic usually handles denormals as having exp=1 but no hidden bit?
    // Simplified: treat e=0 as e=1 if f!=0 for shift distance calculation, but mantissa has no hidden bit.
    // However, the Verilog code uses `ex1 = ex1_pre + {4'd0, ~|ex1_pre}` style logic for bias handling.
    // Let's stick closer to standard hardware alignment logic.

    int32_t exp1 = (e1 == 0) ? 1 : e1; // Denormal treated as exp 1 for diff
    int32_t exp2 = (e2 == 0) ? 1 : e2;
    
    // Add hidden bit logic for internal representation
    uint32_t mant1 = (e1 == 0) ? f1 : (f1 | 1024); // 11 bits (1 integer + 10 fraction)
    uint32_t mant2 = (e2 == 0) ? f2 : (f2 | 1024);

    bool swap = false;
    if (exp1 < exp2) {
        swap = true;
    } else if (exp1 == exp2) {
        if (mant1 < mant2) swap = true;
    }

    uint16_t sign_big = swap ? s2 : s1;
    int32_t  exp_big  = swap ? exp2 : exp1;
    uint32_t mant_big = swap ? mant2 : mant1;

    uint16_t sign_sml = swap ? s1 : s2;
    int32_t  exp_sml  = swap ? exp1 : exp2;
    uint32_t mant_sml = swap ? mant1 : mant2;

    int32_t exp_diff = exp_big - exp_sml;

    // 4. Shift Small Mantissa
    // The Verilog code keeps some extra bits for precision loss detection.
    // It seems to keep at least round/guard/sticky bits concept, 
    // or just a "shifted extension" vector.
    
    // Let's model a 20+ bit accumulator for precision detecting.
    // Shift sml right by exp_diff.
    // Bits shifted out are "loss candidate".
    
    uint32_t mant_sml_shifted = 0;
    uint32_t bits_lost = 0;

    if (exp_diff >= 11 + 2) { 
        // Completely shifted out (except sticky)
        mant_sml_shifted = 0;
        bits_lost = (mant_sml != 0); 
    } else {
        mant_sml_shifted = mant_sml >> exp_diff;
        // Check lost bits
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

    // 6. Normalization
    // If subtraction resulted in negative, or 0, or needs shift.
    // But since we picked BIG first, mant_res_signed >= 0 usually (unless equal and subtract -> 0)
    
    int32_t final_exp = exp_big;
    uint32_t final_mant = mant_res_signed;

    if (final_mant == 0) {
        // Zero result
        ret.res = 0; // Positive zero usually? or signs handle it.
        // IEEE says -0 if both inputs are -0 (and add), else +0.
        // But for distinct subtraction resulting in 0, usually +0.
        if (sign_big == sign_sml && sign_big == 1) ret.res = 0x8000; // -0
        ret.zero = true;
        // Precision lost check logic is tricky here, usually none if valid calc
        if (bits_lost) ret.precision_lost = true; 
        return ret;
    }

    // Renormalize (find MSB)
    // If overflow (carry out from addition)
    if (final_mant >= 2048) { // 12th bit set? (since 1024 is 11th bit 1.0)
        // Shift right 1
        bool lost_lk = (final_mant & 1);
        if (lost_lk) bits_lost = 1; // Sticky accumulation
        final_mant >>= 1;
        final_exp++;
    } else {
        // Find leading one (for subtraction case mostly)
        // e.g., 1.000 - 0.111 = 0.001 -> normalize to 1.xxx
        // Denormal handling is complex here.
        while (final_mant < 1024 && final_exp > 1) { // 1024 is implicit 1
             final_mant <<= 1;
             final_exp--;
        }
        // If exp goes to 1 and still < 1024, it's a denormal result.
        // In this case, exp becomes 0.
        if (final_mant < 1024 && final_exp == 1) {
             final_exp = 0;
        }
    }

    // 7. Rounding/Truncation & Precision Lost Check configuration
    // The Verilog code seems to check `precisionLost` based on shifted out bits.
    // `precisionLost = |sum_extension`
    if (bits_lost) ret.precision_lost = true;

    // 8. Pack Result
    if (final_exp >= 31) {
        ret.overflow = true;
        ret.res = (sign_big << 15) | 0x7C00; // Infinity
    } else {
        ret.res = (sign_big << 15) | (final_exp << 10) | (final_mant & 0x3FF);
    }
    
    // Zero check again (if denormal flushed to zero?)
    if ((ret.res & 0x7FFF) == 0) ret.zero = true;

    return ret;
}


// ----------------------------------------------------------------------------
// Main for Generating Test Vectors
// ----------------------------------------------------------------------------
int main() {
    // Define test cases (input A, input B)
    struct TestCase {
        fp16_t a;
        fp16_t b;
        std::string desc;
    };

    std::vector<TestCase> tests = {
        {0xC0B0, 0x1CC0, "Bug Case 1"}, // -2.75 + 0.00...
        {0x00E0, 0x5060, "Normal + Normal"},
        {0x3C00, 0x3C00, "1.0 + 1.0"},
        {0x3C00, 0xBC00, "1.0 + (-1.0) -> Zero"},
        {0x7C00, 0x3C00, "Inf + 1.0 -> Inf"},
        {0x7FFF, 0x3C00, "NaN + 1.0 -> NaN"},
        // Add more specific precision lost cases here
        {0x5140, 0x1CC0, "Precision Loss Example"} 
        // 0x5140 = 0 10100 0101000000 (Exp 20-15=5, 1.01.. * 2^5 = 32+..)
        // 0x1CC0 = 0 00111 0011000000
    };

    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << " FP16 Adder C++ Reference Model Output\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << "  Input A  |  Input B  || Result  | OF | Z  | NaN | PL | Description\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    for (const auto& t : tests) {
        FP16Result hw = fp16_add_model(t.a, t.b);
        
        std::cout << "  0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << t.a
                  << "   |  0x" << std::setw(4) << t.b 
                  << "   || 0x" << std::setw(4) << hw.res
                  << "  |  " << hw.overflow 
                  << " |  " << hw.zero 
                  << " |  " << hw.nan 
                  << "  |  " << hw.precision_lost
                  << " | " << t.desc << "\n";
    }

    return 0;
}
