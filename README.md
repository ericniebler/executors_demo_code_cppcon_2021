# Code for Eric Niebler's CppCon 2021 talk

```sh
mkdir build
cd build
cmake -G Ninja -DCMAKE_CXX_STANDARD:STRING=23 -DCMAKE_CXX_FLAGS:STRING="/Zc:externConstexpr /EHsc /fsanitize=address" ..
```

## Contributing

Development of demo code happens in the open on GitHub, and we are grateful to the community for contributing bugfixes and improvements. Read below to learn how you can take part in improving demo code.

- [Code of Conduct](./CODE_OF_CONDUCT.md)
- [Contributing Guide](./CONTRIBUTING.md)

# License

This project is made available under the Apache License, version 2.0, with LLVM Exceptions.

See [LICENSE.txt](LICENSE.txt) for details.
