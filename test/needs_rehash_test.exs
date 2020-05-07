defmodule Bcrypt.NeedsRehashTest do
  use ExUnit.Case

  @cost 10
  @hash "$2a$10$MTIzNDU2Nzg5MDEyMzQ1Nej0NmcAWSLR.oP7XOR9HD/vjUuOj100y"
  describe "needs_rehash?/2 callback" do
    test "returns true on invalid hashes" do
      assert Bcrypt.needs_rehash?("", %{cost: @cost})
      assert Bcrypt.needs_rehash?("$2a", %{cost: @cost})
    end

    test "returns false if cost is equal" do
      refute Bcrypt.needs_rehash?(@hash, %{cost: @cost})
    end

    test "returns true if cost is higher" do
      assert Bcrypt.needs_rehash?(@hash, %{cost: @cost + 1})
    end

    test "returns true if cost is lower" do
      assert Bcrypt.needs_rehash?(@hash, %{cost: @cost - 1})
    end
  end
end
