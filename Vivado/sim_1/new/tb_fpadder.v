`timescale 1ns / 1ps

module tb_fpadder_new;
  // Clock and reset signals
  reg clk, rstn;
  
  // Test signals
  reg [15:0] num1, num2;
  reg valid_in;
  wire [15:0] result;
  wire valid_out, overflow, zero, NaN, precisionLost;
  
  // Result analysis
  reg [15:0] result_expected;
  reg [7:0] test_case;

  fpadder uut(
    .clk(clk),
    .rstn(rstn),
    .valid_in(valid_in),
    .num1(num1),
    .num2(num2),
    .valid_out(valid_out),
    .result(result),
    .overflow(overflow),
    .zero(zero),
    .NaN(NaN),
    .precisionLost(precisionLost)
  );

  // Clock generation
  initial begin
    clk = 0;
    forever #5 clk = ~clk; // 100MHz clock
  end

  // --------------------------------------------------------------------------
  // Task: run_test
  // Description: Apply inputs, wait for valid_out, and check result.
  // --------------------------------------------------------------------------
  task run_test;
    input [7:0]  tc_num;
    input [15:0] n1;
    input [15:0] n2;
    input [15:0] expected;
    begin
      test_case = tc_num;
      num1 = n1;
      num2 = n2;
      result_expected = expected;
      
      // 1. Input Stimulus
      @(posedge clk);
      valid_in = 1;
      @(posedge clk);
      valid_in = 0;

      // 2. Wait for Response (with Timeout)
      fork : wait_response
        begin
          wait(valid_out);
          @(posedge clk); // Capture result at stable edge
          
          if (result === expected) begin
            $display("[PASS] Case %2d: %h + %h = %h", 
                     tc_num, n1, n2, result);
          end else begin
            $display("[FAIL] Case %2d: %h + %h = %h (Expected: %h)", 
                     tc_num, n1, n2, result, expected);
            $display("       Flags -> OF:%b Z:%b NaN:%b PL:%b", 
                     overflow, zero, NaN, precisionLost);
          end
          disable wait_response;
        end
        begin
          // Timeout: Wait enough cycles (slightly more than pipeline depth)
          // The current fpadder has roughly 2 pipeline stages (result_r -> output reg)
          repeat(10) @(posedge clk); 
          $display("[FAIL] Case %2d: Timeout (No valid_out asserted)", tc_num);
          disable wait_response;
        end
      join
      
      // 3. Inter-test Delay
      repeat(5) @(posedge clk);
    end
  endtask

  // --------------------------------------------------------------------------
  // Main Test Sequence
  // --------------------------------------------------------------------------
  initial begin
    // Initialize
    rstn = 0;
    valid_in = 0;
    num1 = 0;
    num2 = 0;
    result_expected = 0;
    test_case = 0;
    
    // Reset sequence
    #50 rstn = 1;
    #50;
    
    $display("");
    $display("================================================================");
    $display(" Starting Simulation: tb_fpadder (Updated for fpadder.v)");
    $display("================================================================");

    // Case 1: Bug case
    run_test(1, 16'hc0b0, 16'h1cc0, 16'hc0ae);
    
    // Case 2
    run_test(2, 16'h00e0, 16'h5060, 16'h5060);
    
    // Case 3
    run_test(3, 16'h29a8, 16'he1f9, 16'he1f9);
    
    // Case 4
    run_test(4, 16'h54a5, 16'h1cc0, 16'h54a5);
    
    // Case 5
    run_test(5, 16'h00b8, 16'h0080, 16'h0138);

    // Case 6: Addition with precision lost
    run_test(6, {1'b0, 5'd21, 10'b10100101}, {1'b0, 5'd14, 10'b11001100}, 16'h54ae);

    // Case 7: Addition of two numbers with same exp
    run_test(7, {1'b0, 5'd4, 10'b10100000}, {1'b0, 5'd4, 10'b01101100}, 16'h1486);

    // Case 8: Addition without precision lost
    run_test(8, {1'b0, 5'd10, 10'b11100000}, {1'b0, 5'd12, 10'b01101001}, 16'h31a1);

    // Case 9: Addition different signs without precision lost
    run_test(9, {1'b0, 5'd5, 10'b10101100}, {1'b1, 5'd6, 10'b00101101}, 16'h935c);

    // Case 10: Addition different signs without precision lost
    run_test(10, {1'b1, 5'd13, 10'b00001100}, {1'b0, 5'd13, 10'b11101100}, 16'h2b00);

    // Case 11: Addition different signs without precision lost
    run_test(11, {1'b1, 5'd30, 10'b10101010}, {1'b0, 5'd30, 10'b10101100}, 16'h5400);

    // Case 12: Zero flag
    run_test(12, {1'b1, 5'd25, 10'b10011101}, {1'b0, 5'd25, 10'b10011101}, 16'h8000);

    // Case 13: NaN flag
    run_test(13, {1'b0, 5'b10001, 10'b11111111}, {1'b0, 5'b11111, 10'b11111111}, 16'h7cff);

    // Case 14: Overflow flag
    run_test(14, {1'b0, 5'b11110, 10'b1111111111}, {1'b0, 5'b11110, 10'b1111111111}, 16'h7c00);

    // Case 15: Overflow flag
    run_test(15, {1'b0, 5'b11111, 10'b0000000000}, {1'b0, 5'b10010, 10'b1110000011}, 16'h7c00);

    $display("================================================================");
    $display(" Simulation Finished ");
    $display("================================================================");
    $finish;
  end
  
endmodule
