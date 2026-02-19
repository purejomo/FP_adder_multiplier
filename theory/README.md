# Floating Point Arithmetic Implementation Theory
This document outlines the theoretical background and hardware architecture for implementing IEEE 754 Floating Point Adders and Multipliers, with a focus on Half-Precision (FP16).

## 1. IEEE 754 Representation (FP16)
Half-precision floating-point format (16 bits) consists of:
- **Sign (S)**: 1 bit (Bit 15)
- **Exponent (E)**: 5 bits (Bits 14-10) - Bias is **15**
- **Mantissa (M)**: 10 bits (Bits 9-0) - Implicit leading 1 (hidden bit) for normalized numbers.

Value = $(-1)^S \times (1.M) \times 2^{E - 15}$

---

## 2. Floating Point Adder Architecture
Floating point addition is significantly more complex than integer addition because the decimal points must be aligned before the operation can proceed.

### Core Steps:

### Step 1: Compare Exponents & Swap
- Compare the exponents of the two operands ($E_a$ vs $E_b$).
- Determine the larger number (Big) and the smaller number (Small).
- Calculate the exponent difference: $\Delta E = E_{big} - E_{small}$.
- Set the tentative result exponent to $E_{big}$.

### Step 2: Alignment (Mantissa Shifting)
- The mantissa of the smaller number ($M_{small}$) must be **right-shifted** by $\Delta E$ bits to align with the larger number's scale.
- **Hidden Bit**: Remember to append the hidden '1' (or '0' if subnormal) before shifting.
- **Sticky Bit**: Bits shifted out should ideally be tracked for rounding, though simple implementations may truncate.

### Step 3: Mantissa Addition/Subtraction
- Perform the integer addition or subtraction of the aligned mantissas based on the sign bits.
- If Signs are equal: $M_{res} = M_{big} + M_{shifted\_small}$
- If Signs differ: $M_{res} = M_{big} - M_{shifted\_small}$
  - Note: Since we prioritized the larger number, the result of subtraction will always be positive (or zero), simplifying the sign logic.

### Step 4: Normalization
The result from Step 3 might not be in standard IEEE 754 format (1.xxxxx).
- **Overflow (Carry out)**: If addition resulted in a value $\ge 2.0$ (e.g., $1.1 + 1.1 = 3.0 \rightarrow 11.0_2$):
  - Right shift mantissa by 1.
  - Increment Exponent by 1.
- **Underflow (Leading Zeros)**: If subtraction resulted in a value $< 1.0$ (e.g., $1.1 - 1.05 = 0.05$):
  - Left shift mantissa until the leading bit is 1.
  - Decrement Exponent by the shift amount.
  - *Edge Case*: If the result is exactly zero, set Exponent and Mantissa to 0.

### Step 5: Rounding & Packing
- Apply rounding rules (e.g., Round to Nearest Even) if bits were lost during shifting.
- Handle exponent overflow (result becomes Infinity) or underflow (result becomes Subnormal/Zero).
- Assemble the final Sign, Exponent, and Mantissa fields into the 16-bit word.

---

## 3. Floating Point Multiplier Architecture
Multiplication is structurally different but conceptually simpler in some alignment aspects compared to addition.

### Core Steps:

### Step 1: Sign Calculation
- The sign of the result is simply the XOR of the input signs.
- $S_{res} = S_a \oplus S_b$

### Step 2: Exponent Addition
- Add the two exponents. Since both exponents carry a bias, the bias must be subtracted once to avoid double-biasing.
- $E_{tentative} = E_a + E_b - \text{Bias}$
- *Note*: In FP16, Bias is 15.

### Step 3: Mantissa Multiplication
- Multiply the two mantissas (including the hidden bit).
- FP16 (10+1 bits) $\times$ FP16 (10+1 bits) results in a 22-bit product.
- $M_{product} = (1.M_a) \times (1.M_b)$
- The result will be in the range $[1, 4)$.

### Step 4: Normalization
The product of two normalized numbers $(1.x \times 1.y)$ will result in a value between $1.0$ and just slightly less than $4.0$.
- **No Shift Needed**: If product is in $[1, 2)$.
- **Right Shift**: If product is in $[2, 4)$ (i.e., MSB is at bit position 1 of integer part):
  - Right shift mantissa by 1.
  - Increment Exponent by 1.

### Step 5: Rounding & Packing
- Truncate or round the 22-bit product back to 10 bits.
- Check for Exponent Overflow (Infinity) or Underflow (Zero/Subnormal).
- Pack the bits.

---

## 4. Special Handling Checks (Exceptions)
Both Adder and Multiplier must handle these cases **before** the main pipeline or as a parallel check:
1.  **NaN (Not a Number)**: If any operand is NaN, result is NaN.
2.  **Infinity**:
    - Add: Inf + Normal = Inf. (Inf - Inf = NaN).
    - Mul: Inf * Normal = Inf. (Inf * 0 = NaN).
3.  **Zero**:
    - Mul: Anything * 0 = 0 (except NaN/Inf).

---

## 5. MAC Unit (Multiply-Accumulate) Architecture
The MAC unit performs the fundamental operation $R = (A \times B) + C$. This operation is critical for matrix multiplications, convolutions, and dot products commonly found in Digital Signal Processing (DSP) and Deep Learning applications.

### Discrete vs. Fused MAC
- **Discrete MAC**: Performs multiplication, rounds the result, and then adds it to the accumulator.
  - $R = \text{Round}(\text{Round}(A \times B) + C)$
- **Fused Multiply-Add (FMA)**: Performs multiplication and addition as a single operation with only **one final rounding step**. This provides higher precision (less rounding error).
  - $R = \text{Round}((A \times B) + C)$

### Core Architecture Flow:

### Step 1: Multiplication (Partial Product Generation)
- Calculate $P = A \times B$ similar to the standard FP Multiplier.
- Compute Sign, Exponent, and unnormalized Mantissa product ($M_a \times M_b$).

### Step 2: Alignment with Addend (C)
- Compare the exponent of the product ($E_p$) with the exponent of the addend ($E_c$).
- Align the mantissa of the smaller number to match the larger exponent.
- This often requires a wide shifter (since the product mantissa is typically wider than the addend mantissa before rounding).

### Step 3: Addition
- Add the aligned mantissas.

### Step 4: Normalization & Rounding
- Normalize the result (shift left to find leading 1).
- Perform rounding (e.g., Nearest to Even) only distinctively at this final stage for FMA.
- Check for Overflow/Underflow exceptions.
