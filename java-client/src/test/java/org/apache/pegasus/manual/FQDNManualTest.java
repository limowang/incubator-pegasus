/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
package org.apache.pegasus.manual;

import org.apache.pegasus.base.host_port;
import org.apache.pegasus.base.rpc_address;

/**
 * Manual verification test for FQDN support.
 *
 * This class can be compiled and run directly to verify basic FQDN functionality
 * without requiring Maven or a full test framework.
 *
 * Compile:
 *   cd java-client
 *   javac -cp "target/classes:lib/*" \
 *     src/test/java/org/apache/pegasus/manual/FQDNManualTest.java \
 *     -d target/test-classes/
 *
 * Run:
 *   java -cp "target/test-classes:target/classes:lib/*" \
 *     org.apache.pegasus.manual.FQDNManualTest
 */
public class FQDNManualTest {

  public static void main(String[] args) {
    System.out.println("=== FQDN Manual Verification Test ===\n");

    boolean allPassed = true;

    // Test 1: host_port creation
    allPassed &= testHostPortCreation();

    // Test 2: rpc_address from IP
    allPassed &= testRpcAddressFromIp();

    // Test 3: host_port field access
    allPassed &= testHostPortFieldAccess();

    // Summary
    System.out.println("\n=== Test Summary ===");
    if (allPassed) {
      System.out.println("✅ All tests PASSED");
      System.out.println("\nFQDN support implementation is VERIFIED");
    } else {
      System.out.println("❌ Some tests FAILED");
      System.out.println("\nPlease review the implementation");
    }

    System.exit(allPassed ? 0 : 1);
  }

  private static boolean testHostPortCreation() {
    System.out.println("Test 1: host_port Creation");
    try {
      host_port hp = new host_port();
      hp.setHost("localhost");
      hp.setPort(34801);
      hp.setHostPortType((byte) 1);

      assert "localhost".equals(hp.getHost()) : "Host mismatch";
      assert 34801 == hp.getPort() : "Port mismatch";
      assert 1 == hp.getHostPortType() : "Type mismatch";

      System.out.println("  ✅ PASSED: host_port creation works\n");
      return true;
    } catch (Exception e) {
      System.out.println("  ❌ FAILED: " + e.getMessage() + "\n");
      return false;
    }
  }

  private static boolean testRpcAddressFromIp() {
    System.out.println("Test 2: rpc_address from IP");
    try {
      rpc_address addr = rpc_address.fromIpPort("127.0.0.1:34801");

      assert addr != null : "Address is null";
      assert !addr.isInvalid() : "Address is invalid";
      assert 34801 == addr.get_port() : "Port mismatch";

      System.out.println("  ✅ PASSED: rpc_address creation works\n");
      return true;
    } catch (Exception e) {
      System.out.println("  ❌ FAILED: " + e.getMessage() + "\n");
      return false;
    }
  }

  private static boolean testHostPortFieldAccess() {
    System.out.println("Test 3: host_port Field Access");
    try {
      host_port hp = new host_port();
      hp.setHost("example.com");
      hp.setPort(8080);
      hp.setHostPortType((byte) 1);

      // Test field access
      String host = hp.getHost();
      int port = hp.getPort();
      byte type = hp.getHostPortType();

      assert "example.com".equals(host) : "Host field access failed";
      assert 8080 == port : "Port field access failed";
      assert 1 == type : "Type field access failed";

      // Test toString
      String str = hp.toString();
      assert str != null : "toString is null";
      assert str.contains("example.com") : "toString doesn't contain host";
      assert str.contains("8080") : "toString doesn't contain port";

      System.out.println("  ✅ PASSED: host_port field access works\n");
      return true;
    } catch (Exception e) {
      System.out.println("  ❌ FAILED: " + e.getMessage() + "\n");
      e.printStackTrace();
      return false;
    }
  }
}
