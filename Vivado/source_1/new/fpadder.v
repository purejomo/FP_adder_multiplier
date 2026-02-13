// Pipeline version of 16-bit floating point addition

module fpadder #(
  parameter BIAS = 5'd15
)(
  input  wire        clk,
  input  wire        rstn,
  input  wire        valid_in,
  input  wire [15:0] num1,
  input  wire [15:0] num2,

  output reg         valid_out,
  output reg  [15:0] result,
  output reg         overflow,
  output reg         zero,
  output reg         NaN,
  output reg         precisionLost
);
  
  // Decode numbers
  wire s1, s2;
  wire [4:0] e1_raw, e2_raw;
  wire [9:0] f1, f2;
  assign {s1, e1_raw, f1} = num1;
  assign {s2, e2_raw, f2} = num2;

  // 특수값
  wire n1_is_inf = (&e1_raw) & ~|f1;
  wire n2_is_inf = (&e2_raw) & ~|f2;
  wire n1_is_nan = (&e1_raw) &  |f1;
  wire n2_is_nan = (&e2_raw) &  |f2;
  wire any_nan   = n1_is_nan | n2_is_nan;
  wire any_inf   = n1_is_inf | n2_is_inf;

  wire n1_is_zero = ~|e1_raw & ~|f1;
  wire n2_is_zero = ~|e2_raw & ~|f2;

  wire h1 = |e1_raw;
  wire h2 = |e2_raw;

  wire  e1_gt_e2 = (e1_raw > e2_raw);
  wire  e1_eq_e2 = (e1_raw == e2_raw);
  wire  f1_ge_f2 = (f1 >= f2);
  wire  pick1_as_big = e1_gt_e2 | (e1_eq_e2 & f1_ge_f2);

  wire        big_s   = pick1_as_big ? s1     : s2;
  wire [4:0]  big_e   = pick1_as_big ? e1_raw : e2_raw;
  wire [9:0]  big_f   = pick1_as_big ? f1     : f2;
  wire        big_h   = pick1_as_big ? h1     : h2;

  wire        sml_s   = pick1_as_big ? s2     : s1;
  wire [4:0]  sml_e   = pick1_as_big ? e2_raw : e1_raw;
  wire [9:0]  sml_f   = pick1_as_big ? f2     : f1;
  wire        sml_h   = pick1_as_big ? h2     : h1;

  wire [4:0] big_ex = big_e + {4'd0, ~|big_e};
  wire [4:0] sml_ex = sml_e + {4'd0, ~|sml_e};
  wire [4:0] ex_diff = big_ex - sml_ex;
  
  wire [10:0] big_float = {big_h, big_f};
  wire [10:0] small_float = {sml_h, sml_f};
  wire sameSign = (big_s == sml_s);
  
  wire [10:0] sign_small_float = sameSign ? small_float : (~small_float + 1);
  
  reg [10:0] shifted_small_float;
  reg [9:0] small_extension;
  
  always @* begin
    case (ex_diff[4:0])
      5'd0:  begin shifted_small_float = sign_small_float; small_extension = 10'd0; end
      5'd1:  begin shifted_small_float = {1'b0, sign_small_float[10:1]}; small_extension = {sign_small_float[0], 9'd0}; end
      5'd2:  begin shifted_small_float = {2'b0, sign_small_float[10:2]}; small_extension = sign_small_float[1:0]; end
      5'd3:  begin shifted_small_float = {3'b0, sign_small_float[10:3]}; small_extension = sign_small_float[2:0]; end
      5'd4:  begin shifted_small_float = {4'b0, sign_small_float[10:4]}; small_extension = sign_small_float[3:0]; end
      5'd5:  begin shifted_small_float = {5'b0, sign_small_float[10:5]}; small_extension = sign_small_float[4:0]; end
      5'd6:  begin shifted_small_float = {6'b0, sign_small_float[10:6]}; small_extension = sign_small_float[5:0]; end
      5'd7:  begin shifted_small_float = {7'b0, sign_small_float[10:7]}; small_extension = sign_small_float[6:0]; end
      5'd8:  begin shifted_small_float = {8'b0, sign_small_float[10:8]}; small_extension = sign_small_float[7:0]; end
      5'd9:  begin shifted_small_float = {9'b0, sign_small_float[10:9]}; small_extension = sign_small_float[8:0]; end
      5'd10: begin shifted_small_float = {10'b0, sign_small_float[10]}; small_extension = sign_small_float[9:0]; end
      default: begin shifted_small_float = 11'd0; small_extension = sign_small_float[9:0]; end
    endcase
  end

  wire sum_carry;
  wire [10:0] sum;
  assign {sum_carry, sum} = big_float + shifted_small_float;

  wire zeroSmall = ~(|sml_ex | |sml_f);
  
  reg [3:0] shift_am;
  always @* begin
    casex(sum)
      11'b1xxxxxxxxxx: shift_am = 4'd0;
      11'b01xxxxxxxxx: shift_am = 4'd1;
      11'b001xxxxxxxx: shift_am = 4'd2;
      11'b0001xxxxxxx: shift_am = 4'd3;
      11'b00001xxxxxx: shift_am = 4'd4;
      11'b000001xxxxx: shift_am = 4'd5;
      11'b0000001xxxx: shift_am = 4'd6;
      11'b00000001xxx: shift_am = 4'd7;
      11'b000000001xx: shift_am = 4'd8;
      11'b0000000001x: shift_am = 4'd9;
      default: shift_am = 4'd10;
    endcase
  end

  reg [9:0] sum_shifted;
  reg [9:0] sum_extension;
  always @* begin
    case (shift_am)
      4'd0: sum_shifted =  sum[9:0];
      4'd1: sum_shifted = {sum[8:0], sum_extension[9]};
      4'd2: sum_shifted = {sum[7:0], sum_extension[9:8]};
      4'd3: sum_shifted = {sum[6:0], sum_extension[9:7]};
      4'd4: sum_shifted = {sum[5:0], sum_extension[9:6]};
      4'd5: sum_shifted = {sum[4:0], sum_extension[9:5]};
      4'd6: sum_shifted = {sum[3:0], sum_extension[9:4]};
      4'd7: sum_shifted = {sum[2:0], sum_extension[9:3]};
      4'd8: sum_shifted = {sum[1:0], sum_extension[9:2]};
      4'd9: sum_shifted = {sum[0],  sum_extension[9:1]};
      default: sum_shifted = sum_extension;
    endcase
    case (shift_am)
      4'd0: sum_extension = small_extension;
      4'd1: sum_extension = small_extension[8:0];
      4'd2: sum_extension = small_extension[7:0];
      4'd3: sum_extension = small_extension[6:0];
      4'd4: sum_extension = small_extension[5:0];
      4'd5: sum_extension = small_extension[4:0];
      4'd6: sum_extension = small_extension[3:0];
      4'd7: sum_extension = small_extension[2:0];
      4'd8: sum_extension = small_extension[1:0];
      4'd9: sum_extension = small_extension[0];
      default: sum_extension = 10'd0;
    endcase
  end

  wire neg_exp = (big_ex < shift_am);
  wire [4:0] res_exp_same_s = big_ex + {4'd0, (~zeroSmall & sum_carry & sameSign)} - {4'd0,({1'b0,sum[9:0]} == sum)};
  wire [4:0] res_exp_diff_s = (neg_exp | (shift_am == 4'd10)) ? 5'd0 : (~shift_am + big_ex + 5'd1);

  wire [4:0] res_exp = sameSign ? res_exp_same_s : res_exp_diff_s;
  wire [9:0] res_frac = zeroSmall ? big_f : ((sameSign) ? ((sum_carry) ? sum[10:1] : sum[9:0]) : ((neg_exp) ? 10'd0 : sum_shifted));

  wire res_zero = (num1[14:0] == num2[14:0]) & (~num1[15] == num2[15]);
  wire res_overflow = ((&big_ex[4:1] & ~big_ex[0]) & sum_carry & sameSign) | any_inf;
  wire res_nan = any_nan | (n1_is_inf & n2_is_inf & (s1 ^ s2));
  reg res_precisionLost;
  
  always @* begin
    case (shift_am)
      4'd0: res_precisionLost = |sum_extension;
      4'd1: res_precisionLost = |sum_extension[8:0];
      4'd2: res_precisionLost = |sum_extension[7:0];
      4'd3: res_precisionLost = |sum_extension[6:0];
      4'd4: res_precisionLost = |sum_extension[5:0];
      4'd5: res_precisionLost = |sum_extension[4:0];
      4'd6: res_precisionLost = |sum_extension[3:0];
      4'd7: res_precisionLost = |sum_extension[2:0];
      4'd8: res_precisionLost = |sum_extension[1:0];
      4'd9: res_precisionLost = sum_extension[0];
      default: res_precisionLost = 1'b0;
    endcase
  end

  // ---------- Stage 2: Pipeline registers ----------
  reg [15:0] result_r;
  reg        overflow_r, zero_r, nan_r, precisionLost_r;
  reg        valid_r1;

  always @(posedge clk or negedge rstn) begin
    if (!rstn) begin
      result_r        <= 16'd0;
      overflow_r      <= 1'b0;
      zero_r          <= 1'b0;
      nan_r           <= 1'b0;
      precisionLost_r <= 1'b0;
      valid_r1        <= 1'b0;
    end else begin
      result_r        <= {big_s, res_exp, res_frac} | {16{res_overflow}};
      overflow_r      <= res_overflow;
      zero_r          <= res_zero;
      nan_r           <= res_nan;
      precisionLost_r <= res_precisionLost;
      valid_r1        <= valid_in;
    end
  end

  // ---------- Output registers ----------
  always @(posedge clk or negedge rstn) begin
    if (!rstn) begin
      valid_out     <= 1'b0;
      result        <= 16'd0;
      overflow      <= 1'b0;
      zero          <= 1'b0;
      NaN           <= 1'b0;
      precisionLost <= 1'b0;
    end else begin
      valid_out     <= valid_r1;
      result        <= result_r;
      overflow      <= overflow_r;
      zero          <= zero_r;
      NaN           <= nan_r;
      precisionLost <= precisionLost_r;
    end
  end

endmodule
