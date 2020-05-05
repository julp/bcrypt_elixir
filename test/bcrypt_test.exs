defmodule BcryptTest do
  use ExUnit.Case
  doctest Bcrypt

  import Comeonin.BehaviourTestHelper

  test "implementation of Comeonin.PasswordHash behaviour" do
    password = Enum.random(ascii_passwords())
    assert correct_password_true(Bcrypt, password)
    assert wrong_password_false(Bcrypt, password)
  end

  test "Comeonin.PasswordHash behaviour with non-ascii characters" do
    password = Enum.random(non_ascii_passwords())
    assert correct_password_true(Bcrypt, password)
    assert wrong_password_false(Bcrypt, password)
  end

  test "hash legacy prefix" do
    assert String.starts_with?(Bcrypt.hash(""), "$2b$")
    assert String.starts_with?(Bcrypt.hash("", %{legacy: true}), "$2a$")
    assert String.starts_with?(Bcrypt.hash("", %{legacy: false}), "$2b$")
  end

  test "gen_salt number of rounds" do
    assert String.starts_with?(Bcrypt.gen_salt(), "$2b$12$")
    assert String.starts_with?(Bcrypt.gen_salt(8), "$2b$08$")
    assert String.starts_with?(Bcrypt.gen_salt(20), "$2b$20$")
  end

  test "gen_salt length of salt" do
    assert byte_size(Bcrypt.gen_salt(8)) == 29
    assert byte_size(Bcrypt.gen_salt(20)) == 29
  end

  test "wrong input to gen_salt" do
    assert String.starts_with?(Bcrypt.gen_salt(3), "$2b$04$")
    assert String.starts_with?(Bcrypt.gen_salt(32), "$2b$31$")
  end

  test "gen_salt with support for $2a$ prefix" do
    assert String.starts_with?(Bcrypt.gen_salt(8, true), "$2a$08$")
    assert String.starts_with?(Bcrypt.gen_salt(12, true), "$2a$12$")
  end
end
