defmodule Comeonin.BehaviourTestHelper do
  @moduledoc """
  Test helper functions for Comeonin behaviours.
  """

  @doc ~S"""
  List of passwords that just contain basic ascii characters.
  """
  def ascii_passwords do
    ["passw0rd", "hard2guess", "!@#$%^&* ", "1q2w3e4r5t"]
  end

  @doc ~S"""
  List of passwords that contain non-ascii characters.
  """
  def non_ascii_passwords do
    ["påsswörd", "aáåä eéê ëoôö", "мадам, я доктор, вот банан", "Я❤três☕ où☔"]
  end

  @doc ~S"""
  Checks that the verify? function returns true for correct password.
  """
  def correct_password_true(module, password) do
    module.verify?(password, module.hash(password))
  end

  @doc ~S"""
  Checks that the verify? function returns false for incorrect passwords.
  """
  def wrong_password_false(module, password) do
    hash = module.hash(password)

    password
    |> wrong_passwords()
    |> Enum.all?(&(module.verify?(&1, hash) == false))
  end

  @doc ~S"""
  Checks that the add_hash function creates a map with the password_hash set.
  """
  def add_hash_creates_map(module, password) do
    %{password_hash: hash} = module.add_hash(password)
    module.verify?(password, hash)
  end

  @doc ~S"""
  Checks that the check_pass function returns the user for correct passwords.
  """
  def check_pass_returns_user(module, password) do
    hash = module.hash(password)
    user = %{id: 2, name: "fred", password_hash: hash}
    module.check_pass(user, password) == {:ok, user}
  end

  @doc ~S"""
  Checks that the check_pass function returns an error for incorrect passwords.
  """
  def check_pass_returns_error(module, password) do
    hash = module.hash(password)
    user = %{id: 2, name: "fred", password_hash: hash}

    password
    |> wrong_passwords()
    |> Enum.all?(&(module.check_pass(user, &1) == {:error, "invalid password"}))
  end

  @doc ~S"""
  Checks that the check_pass function returns an error when no user is found.
  """
  def check_pass_nil_user(module) do
    module.check_pass(nil, "password") == {:error, "invalid user-identifier"}
  end

  defp wrong_passwords(password) do
    words = [password, String.duplicate(password, 2)]
    reversed = Enum.map(words, &String.reverse(&1))
    Enum.flat_map(words ++ reversed, &slices/1)
  end

  defp slices(password) do
    ranges = [{1, -1}, {0, -2}, {2, -1}, {2, -2}]
    for {first, last} <- ranges, do: String.slice(password, first..last)
  end
end
