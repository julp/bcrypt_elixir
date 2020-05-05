defmodule BcryptElixir.Mixfile do
  use Mix.Project

  @version "2.2.0"

  @description """
  Bcrypt password hashing algorithm for Elixir
  """

  defp elixirc_paths(:test), do: ~W[lib test/support]
  defp elixirc_paths(_), do: ~W[lib]

  def project do
    [
      app: :bcrypt_elixir,
      version: @version,
      elixir: "~> 1.7",
      start_permanent: Mix.env() == :prod,
      compilers: [:elixir_make] ++ Mix.compilers(),
      description: @description,
      package: package(),
      source_url: "https://github.com/riverrun/bcrypt_elixir",
      elixirc_paths: elixirc_paths(Mix.env()),
      deps: deps(),
      dialyzer: [
        plt_file: {:no_warn, "priv/plts/dialyzer.plt"}
      ]
    ]
  end

  def application do
    [
      extra_applications: [:logger, :crypto]
    ]
  end

  defp deps do
    [
      if :inet.gethostname() == {:ok, 'freebsd'} do
        {:expassword, path: "~/elixir/expassword"}
      else
        {:expassword, git: "https://github.com/julp/expassword.git", branch: "master"}
      end,
      {:elixir_make, "~> 0.6", runtime: false},
      {:ex_doc, "~> 0.20", only: :dev, runtime: false},
      {:dialyxir, "~> 1.0.0-rc.3", only: :dev, runtime: false}
    ]
  end

  defp package do
    [
      files: ["lib", "c_src", "mix.exs", "Makefile*", "README.md", "LICENSE"],
      maintainers: ["David Whitlock"],
      licenses: ["BSD"],
      links: %{"GitHub" => "https://github.com/riverrun/bcrypt_elixir"}
    ]
  end
end
