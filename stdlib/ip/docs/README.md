# `ip` Package

## Depends On
  * `vector`
  * `types`
  * `exceptions`
  * `util`
  * `string`

Classes
---
## `Address`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes

### Constructors
#### `constructor()`
Constructs an IPv4 `Address` with all components initialized to 0

#### `constructor(isIPv6: bool)`
Constructs an `Address` with all components intialized to 0

**Parameters**:
  * `isIPv6` - If `true`, the new address will be IPv6. Otherwise, it will be IPv4

#### <a name="constructor(address: [string].String)">`constructor(address: [string].String)`</a>
Constructs an `Address` by parsing the given string as an IPv4 or IPv6 address

**Parameters**:
  * `address` - The string to parse as an address

**Is cast-constructor**: Yes

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(address: [string].String)`](#constructor%28address%3A%20%5Bstring%5D.String%29)
.

#### to `[string].String`
**Returns**: A string representation of this Address\
**Notes**:
  * IPv6 addresses will be represented with full shorthand (i.e. leading zeros and consecutive zero components omitted) and components will be in hexadecimal format
  * IPv4 addresses will be represented normally&mdash;i.e. decimal formatted components joined with dots (`.`)

### Accessors
#### `isIPv4: bool`
**Read**: Yes\
**Write**: No

If true, this address is an [IPv4](https://en.wikipedia.org/wiki/IPv4) address.

#### `isIPv6: bool`
**Read**: Yes\
**Write**: No

If true, this address is an [IPv6](https://en.wikipedia.org/wiki/IPv6) address.

### Operators
#### `this[[types].uint8]: ref [types].uint16`
Accesses a component at the specified index

**Parameters**:
  * The index of the desired component

**Returns**: A reference to the component at the index\
**Note**: Throws an InvalidComponentIndex error when trying to access past the:
  * 3rd index (base-0) for IPv4
  * 7th index (base-0) for IPv6

Exceptions
---
## `InvalidComponentIndex`
An invalid address component index was provided

An exception thrown by Address's subscript operator
when the provided subscript index exceeds the maximum
for the Address:
  * 3 (base-0) for IPv4
  * 7 (base-0) for IPv6

## `InvalidAddress`
An invalid address string was provided

An exception thrown by Address's String cast constructor
when the provided string is invalid for either IPv4 or IPv6.

For IPv4:
  * If less than 3 dots are found
  * If more than 3 dots are found

For IPv6:
  * If less than 8 components are found (and no consecutive shorthand is found)
  * If multiple consecutive shorthands are found
  * If more than 8 components are found
