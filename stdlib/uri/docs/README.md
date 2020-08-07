# `uri` Package

## Depends On
  * `string`
  * `ip`
  * `exceptions`
  * `types`
  * `vector`
  * `util`
  * `unicode`
  * `map`

Classes
---
## `URI`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes

### Members

#### `scheme: [string].String`
The scheme of this URI

**Note**: ... as defined by [RFC 2396](https://www.ietf.org/rfc/rfc2396.txt), section 3.1

#### `authority: Authority?`
The authority component of this URI

#### `path: Path`
The path components of this URI

#### `query: Query?`
The query component of this URI

#### `fragment: [string].String?`
The fragment component of this URI

### Constructors

#### `constructor()`
Constructs a new, completely empty URI

#### <a name="URI.constructor(data: [string].String)">`constructor(data: [string].String)`</a>
Constructs a new URI by parsing the given string

**Parameters**:
  * `data` - The string to parse

**Is cast-constructor**: Yes

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(data: [string].String)`](#URI.constructor%28data%3A%20%5Bstring%5D.String%29)

#### to `[string].String`
**Returns**: A fully encoded and valid string representation of this URI\
**Note**: The generated string fully conforms to [RFC 2396](https://www.ietf.org/rfc/rfc2396.txt)

## `Query`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes\
**Parent**: `[map].Map<[string].String, QueryValue>`

### Constructors
#### `constructor()`
Constructs a new Query with no entries

#### <a name="Query.constructor(data: [string].String)">`constructor(data: [string].String)`</a>
Constructs a new Query, filling it with entries by parsing the given string

**Parameters**:
  * `data` - The string to parse and create entries from

**Is cast-constructor**: Yes

### Members
#### `keyDelimiter: byte`
The character to use as a key delimiter; that is, it will be used to determine where
one entry stops and another one starts

**Default Value**: `'&'`\
**Note**: This is typically either `&` or `;`

### Operators

#### `this = [string].String: ref Query`
Parses the given string and replaces all of this Query's entries
with new ones from the parsed string

**Parameters**:
  * The desired new value for this Query

**Returns**: A reference to the current Query

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(data: [string].String)`](#Query.constructor%28data%3A%20%5Bstring%5D.String%29)

#### to `[string].String`
**Returns**: A string representation of this Query\
**Note**: Keys and values will be encoded automatically in the resulting string

## `QueryValue`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes\
**Parent**: `[vector].Vector<[string].String>`

### Constructors
#### `constructor()`
Constructs an empty QueryValue

#### <a name="QueryValue.constructor(data: [string].String)">`constructor(data: [string].String)`</a>
Constructs a new QueryValue and adds the given string
as a new entry

**Is cast-constructor**: Yes

#### <a name="QueryValue.constructor(data: ref [vector].Vector<[string].String>)">`constructor(data: ref [vector].Vector<[string].String>)`</a>
Constructs a new QueryValue by copying entries from a
string vector

**Is cast-constructor**: Yes

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(data: [string].String)`](#QueryValue.constructor%28data%3A%20%5Bstring%5D.String%29)

#### from `ref [vector].Vector<[string].String>`
**Is from cast-constructor**: Yes

See [`constructor(data: ref [vector].Vector<[string].String>)`](#QueryValue.constructor%28data%3A%20ref%20%5Bvector%5D.Vector%3C%5Bstring%5D.String%3E%29)

#### to `[string].String`
**Returns**: The first entry in this container\
**Note**: This will throw an `InvalidContainer` exception if there is
          not *exactly* one entry in this container

## `Path`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes\
**Parent**: `[vector].Vector<[string].String>`

### Constructors
#### `constructor()`
Constructs an empty Path (i.e. a path with no components)

#### <a name="Path.constructor(data: [string].String)">`constructor(data: [string].String)`</a>
Constructs a new Path by parsing the given string

**Parameters**:
  * `data` - The string to parse

**Is cast-constructor**: Yes

#### <a name="Path.constructor(data: ref [vector].Vector<[string].String>)">`constructor(data: ref [vector].Vector<[string].String>)`</a>
Constructs a new Path by copying entries from the given string vector

**Parameters**:
  * `path` - The string vector to copy from

**Is cast-constructor**: Yes

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(data: [string].String)`](#Path.constructor%28data%3A%20%5Bstring%5D.String%29)

#### from `ref [vector].Vector<[string].String>`
**Is from cast-constructor**: Yes

See [`constructor(data: ref [vector].Vector<[string].String>)`](#Path.constructor%28data%3A%20ref%20%5Bvector%5D.Vector%3C%5Bstring%5D.String%3E%29)

#### to `[string].String`
**Returns**: A cannonical string representation for this path\
**Note**: Path components will be automatically encoded in the resulting string

## `Authority`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes

### Members
#### `userInformation: UserInformation?`
Contains some user information, such as username and password

#### `host: Host`
Indicates the location of the host (i.e. hostname or address)

#### `port: [types].uint16`
The target port on the host

### Constructors
#### `constructor()`
Constructs an empty Authority

#### <a name="Authority.constructor(data: [string].String)">`constructor(data: [string].String)`</a>
Constructs an Authority by parsing a string

**Parameters**:
  * `data` - The string to parse

**Is cast-constructor**: Yes

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(data: [string].String)`](#Authority.constructor%28data%3A%20%5Bstring%5D.String%29)

#### to `[string].String`
**Returns**: A cannonical string representation for this authority\
**Note**: Components will be automatically encoded in the resulting string

## `UserInformation`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes

### Members
#### `username: String`
A username for the user

#### `password: String?`
A password for the user

### Constructors
#### `constructor()`
Constructs an empty UserInformation instance

#### <a name="UserInformation.constructor(data: [string].String)">`constructor(data: [string].String)`</a>
Constructs a UserInformation instance by parsing a string

**Parameters**:
  * `data` - The string to parse

**Is cast-constructor**: Yes

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(data: [string].String)`](#UserInformation.constructor%28data%3A%20%5Bstring%5D.String%29)

#### to `[string].String`
**Returns**: A string representation of the information in this instance\
**Note**: Information will be automatically encoded in the resulting string

## `Host`
**Defines copy constructor**: No\
**Defines destructor**: No\
**Has copy constructor**: Yes\
**Has destructor**: Yes

### Members

#### `hostname: String?`
The hostname for this Host

#### `address: [ip].Address?`
The address for this Host

### Constructors

#### `constructor()`
Constructs a new, empty Host (with no hostname or address)

#### <a name="Host.constructor(address: [ip].Address)">`constructor(address: [ip].Address)`</a>
Constructs a new Host with the given Address

**Parameters**:
  * `address` - The address of the host

**Is cast-constructor**: Yes

#### <a name="Host.constructor(data: [string].String)">`constructor(data: [string].String)`</a>
Constructs a new Host by parsing the given string

**Parameters**:
  * `data` - The string to parse

**Is cast-constructor**: Yes\
**Note**: The string will first try to be parsed as an address
and will only be interpreted as a hostname if parsing it
as an address fails

### Cast Operators
#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(address: [ip].Address)`](#Host.constructor%28address%3A%20%5Bip%5D.Address%29)

#### from `[string].String`
**Is from cast-constructor**: Yes

See [`constructor(data: [string].String)`](#Host.constructor%28data%3A%20%5Bstring%5D.String%29)

#### to `[string].String`
**Returns**: A string representation of this host\
**Note**: If neither an address nor hostname is present,
this method will throw an `InvalidContainer` exception

#### to `[ip].Address`
**Returns**: An Address representing the address of this Host\
**Note**: If an address is not present for this Host, this method
will throw an `InvalidContainer` exception

### Accessors

#### `isHostname: bool`
**Read**: Yes\
**Write**: No

If true, this Host contains a hostname

#### `isAddress: bool`
**Read**: Yes\
**Write**: No

If true, this Host contains an address

Functions
---
## `encode(data: [string].String): [string].String`
Encodes the given string using percent-encoding

**Parameters**:
  * `data` - A string to encode

**Returns**: A percent-encoded string equivalent to the
input string `data`

## `decode(data: [string].String): [string].String`
Decodes the given percent-encoded string

**Parameters**:
  * `data` - A percent-encoded string to decode

**Returns**: A regular, non-percent-encoded string equivalent
to the input string `data`

Exceptions
---

## `InvalidContainer`
Indicates that container was invalid for the desired operation

This is thrown by the Host class when an attempt is made
to perform an operation on Host that doesn't have the necessary
properties for the operation. For example, if no address
is present in the Host but an attempt is made to cast the Host
to an `[ip].Address`, this exception will be thrown

## `InvalidHostname`
Indicates that the given hostname was malformed

This is thrown by the Host class when parsing a string provided to
the `[string].String` cast constructor and an opening brace (such
as one that would need to be provided to indicate an IPv6 address)
is present but no closing brace is found
