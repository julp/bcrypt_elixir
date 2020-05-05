defmodule Bcrypt.GetOptionsTest do
  use ExUnit.Case

  test "get_options on valid bcrypt hash" do
    assert {:ok, %{cost: 10}} = Bcrypt.get_options("$2a$10$MTIzNDU2Nzg5MDEyMzQ1Nej0NmcAWSLR.oP7XOR9HD/vjUuOj100y")
    assert {:ok, %{cost: 11}} = Bcrypt.get_options("$2a$11$MTIzNDU2Nzg5MDEyMzQ1Nej0NmcAWSLR.oP7XOR9HD/vjUuOj100y")
  end

  test "get_options on truncated hash" do
    assert {:error, :invalid} = Bcrypt.get_options("$2a$10$MTIzNDU2Nzg5MDEyMzQ1Nej0NmcAWSLR.oP7XOR9HD/vjUuOj100")
  end

  test "get_options on non-bcrypt hash" do
    assert {:error, :invalid} = Bcrypt.get_options("$1$rasmusle$rISCgZzpwk3UhDidwXvin0")
  end
end
