# ip
![Package Name](https://img.shields.io/badge/dynamic/yaml?color=%2332a852&label=Package%20Name&query=%24.name&url=https%3A%2F%2Fraw.githubusercontent.com%2Falta-lang%2Fip%2Fmaster%2Fpackage.alta.yaml)
![Version](https://img.shields.io/badge/dynamic/yaml?color=a61900&label=Version&query=%24.version&url=https%3A%2F%2Fraw.githubusercontent.com%2Falta-lang%2Fip%2Fmaster%2Fpackage.alta.yaml)
[![License](https://img.shields.io/github/license/alta-lang/ip?color=%23428bff)](LICENSE)

An IP address parsing/serialization library for Alta.
Fully supports (standard) IPv6 shorthand syntax and (non-standard) IPv4 shorthand.

## Documentation
Check out the [docs](docs).

## Example Usage
Assuming you've done the following at the top of your module:
```alta
import Address from "ip"    # this module's Address class
import String from "string" # the stdlib's String class
import printLine from "io"  # the stdlib's printLine function
```

Parse an IP string:
```alta
let ipv4 = new Address("14.323.643.66")
let ipv6 = new Address("0053:0000:0000:0000:000f:0fcf:3400:005f")

# shorthand: leading zeros omitted
let noLeadingZeros = new Address("53:0:0:0:f:fcf:3400:5f")

# shorthand: consecutive zeros omitted
let noConsecutiveZeros = new Address("0053::000f:0fcf:3400:005f")

# shorthand: full shorthand (both omitted)
let fullShorthand = new Address("53::f:fcf:3400:5f")
```

Serialize an IP object:
```alta
# assuming the same objects as the example above...
let ipv4String = ipv4 as String # "14.323.643.66"

# serialization is implemented as a cast method, which means
# that automatic conversion is available. this allows it to be
# used, for example, with printLine:
printLine("my shortened ipv6 string: ", ipv6)

# all the ipv6 objects are represented the same way:
# they will all be printed in full shorthand syntax
# ("53::f:fcf:3400:5f" in this example)
printLine("original: ", ipv6)
printLine("no-leading-zeros shorthand: ", noLeadingZeros)
printLine("no-consecutive-zeros shorthand: ", noConsecutiveZeros)
printLine("full shorthand: ", fullShorthand)
```

Access a specific component:
```alta
# again, assuming the same objects as the examples above...

# prints "14" for ipv4[0]
printLine("the first component of the IPv4 address is ", ipv4[0])

# prints "0" for ipv6[2]
printLine("the third component of the IPv6 address is ", ipv6[2])

# remember ipv6 components are hexadecimals when parsing and serializing
# but components are accessed as unsigned integers
# ipv6[4] is actually "f", but as an unsigned integer, that's 15, so...
# prints "15" for ipv6[4]
printLine("the fifth component of the IPv6 address is ", ipv6[4])

# all IPv4 addresses have 4 components
# all IPv6 addresses have 8 components

# trying to access an invalid index will yield an InvalidComponentIndex error
# printLine("6th component of the IPv4 address = ", ipv4[5])
```

Create a new Address from scratch:
```alta
# by default, `Address`es are ipv4
let newIPv4 = new Address
let anotherIPv4: Address

let newIPv6 = new Address(true)
# in true Alta fashion, you can also specify the parameter name
# (which helps with self-documenting code)
let anotherIPv6 = new Address(isIPv6: true)
```

Detect if an Address is IPv4 or IPv6:
```alta
# assuming the same objects as the examples above...
if ipv4.isIPv6 {
  # huh, something's wrong...
}

# assuming we created an address called `userInputAddr`
if userInputAddr.isIPv4 {
  # it's IPv4
} else {
  # it's IPv6
}
```

## Notes
### IPv4
According to the standards, IPv4 address should only be represented as either a single, 32-bit decimal or 4 8-bit decimals. These two forms look like `a` or `a.b.c.d`. However, the [inet_aton](https://linux.die.net/man/3/inet_aton) function commonly used on Linux (and some other Unix OSes like macOS) also supports two more address representations: `a.b` and `a.b.c`.

The first form (`a.b`) is the first octet (i.e. 8-bit decimal) followed by a 24-bit decimal representing the last three octects of the address. The second form (`a.b.c`) consists of the first two octets followed by a 16-bit value representing the last two octets.

Note that for all the forms with multiple parts representing the final decimal (`a.b`, `a.b.c`, and `a.b.c.d`), the octets are orded in LE (little-endian) order, meaning that `192` in `192.168.1.1` represents the highest octet (bits 24-32) in the full 32-bit decimal value for the address, for example.
