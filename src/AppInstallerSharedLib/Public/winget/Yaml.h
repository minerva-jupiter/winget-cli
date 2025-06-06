// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include <AppInstallerSHA256.h>

#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <vector>


namespace AppInstaller::YAML
{
    // A location within the stream.
    struct Mark
    {
        Mark() = default;
        Mark(size_t l, size_t c) : line(l), column(c) {}

        size_t line = 0;
        size_t column = 0;
    };

    // An exception from YAML.
    struct Exception : public wil::ResultException
    {
        // The type of error that occurred.
        enum class Type
        {
            None,
            Memory,
            Reader,
            Scanner,
            Parser,
            Composer,
            Writer,
            Emitter,
            Policy,
        };

        // Should only be used for Memory.
        Exception(Type type);

        // Should only be used for Reader.
        Exception(Type type, const char* problem, size_t offset, int value);

        // Used for Scanner, Parser, and Composer.
        Exception(Type type, const char* problem, const Mark& problemMark, const char* context = {}, const Mark& contextMark = {});

        // Used for Writer and Emitter.
        Exception(Type type, const char* problem);

        const char* what() const noexcept override;

        const Mark& GetMark() const;

    private:
        std::string m_what;
        YAML::Mark m_mark;
    };

    // A YAML node.
    struct Node
    {
        // The node's type.
        enum class Type
        {
            Invalid,
            None,
            Scalar,
            Sequence,
            Mapping
        };

        // The node's tag
        enum class TagType
        {
            Unknown,
            Null,
            Bool,
            Str,
            Int,
            Float,
            Timestamp,
            Seq,
            Map,
        };

        Node() : m_type(Type::Invalid), m_tagType(TagType::Unknown) {}
        Node(Type type, std::string tag, const Mark& mark);

        // Sets the scalar value of the node.
        void SetScalar(std::string value);
        void SetScalar(std::string value, bool isQuoted);

        // Adds a child node to the sequence.
        template <typename... Args>
        Node& AddSequenceNode(Args&&... args)
        {
            Require(Type::Sequence);
            return m_sequence->emplace_back(std::forward<Args>(args)...);
        }

        // Merges sequence nodes. If both sequence have the specified key with the same value
        // they will get merged together. All elements in sequence must have the key.
        void MergeSequenceNode(Node other, std::string_view key, bool caseInsensitive = false);

        // Adds a child node to the mapping.
        template <typename... Args>
        Node& AddMappingNode(Node&& key, Args&&... args)
        {
            Require(Type::Mapping);
            return m_mapping->emplace(std::move(key), Node(std::forward<Args>(args)...))->second;
        }

        // Merge mapping node. If both contain a node with the same key preserve this.
        void MergeMappingNode(Node other, bool caseInsensitive = false);

        bool IsDefined() const { return m_type != Type::Invalid; }
        bool IsNull() const { return m_type == Type::Invalid || m_type == Type::None || (m_type == Type::Scalar && m_scalar.empty()); }
        bool IsScalar() const { return m_type == Type::Scalar; }
        bool IsSequence() const { return m_type == Type::Sequence; }
        bool IsMap() const { return m_type == Type::Mapping; }
        Type GetType() const { return m_type; }
        TagType GetTagType() const { return m_tagType; }

        explicit operator bool() const { return IsDefined(); }

        // Gets the scalar value as the requested type.
        template <typename T>
        T as() const
        {
            Require(Type::Scalar);
            T* t = nullptr;
            return as_dispatch(t);
        }

        template <typename T>
        std::optional<T> try_as() const
        {
            if (m_type != Type::Scalar)
            {
                return {};
            }

            T* t = nullptr;
            return try_as_dispatch(t);
        }

        bool operator<(const Node& other) const;

        // Gets a child node from the mapping by its name.
        Node& operator[](std::string_view key);
        const Node& operator[](std::string_view key) const;

        // Gets a child node from the mapping by its name case-insensitive.
        Node& GetChildNode(std::string_view key);
        const Node& GetChildNode(std::string_view key) const;

        // Gets a child node from the sequence by its index.
        Node& operator[](size_t index);
        const Node& operator[](size_t index) const;

        // Gets the number of child nodes.
        size_t size() const;

        // Gets the mark for this node.
        const Mark& Mark() const { return m_mark; }

        // Gets the nodes in the sequence.
        const std::vector<Node>& Sequence() const;

        // Gets the nodes in the mapping.
        const std::multimap<Node, Node>& Mapping() const;

    private:
        Node(std::string_view key) : m_type(Type::Scalar), m_scalar(key), m_tagType(TagType::Str) {}

        // Require certain node types to; throwing if the requirement is not met.
        void Require(Type type) const;

        // The workers for the as function.
        std::string as_dispatch(std::string*) const;
        std::optional<std::string> try_as_dispatch(std::string*) const;

        std::wstring as_dispatch(std::wstring*) const;
        std::optional<std::wstring> try_as_dispatch(std::wstring*) const;

        int64_t as_dispatch(int64_t*) const;
        std::optional<int64_t> try_as_dispatch(int64_t*) const;

        int as_dispatch(int*) const;
        std::optional<int> try_as_dispatch(int*) const;

        bool as_dispatch(bool*) const;
        std::optional<bool> try_as_dispatch(bool*) const;

        Type m_type;
        std::string m_tag;
        TagType m_tagType;
        YAML::Mark m_mark;
        std::string m_scalar;
        std::optional<std::vector<Node>> m_sequence;
        std::optional<std::multimap<Node, Node>> m_mapping;
    };

    // Loads from the input; returns the root node of the first document.
    Node Load(std::string_view input);
    Node Load(const std::string& input);
    Node Load(const std::filesystem::path& input);
    Node Load(const std::filesystem::path& input, Utility::SHA256::HashBuffer& hashOut);

    // Any emitter event.
    // Not using enum class to enable existing code to function.
    enum EmitterEvent
    {
        BeginSeq,
        EndSeq,
        BeginMap,
        EndMap,
        Key,
        Value,
    };

    // Sets the scalar style to use for the next scalar output.
    enum class ScalarStyle
    {
        Any,
        Plain,
        SingleQuoted,
        DoubleQuoted,
        Literal,
        Folded,
    };

    // A schema header for a document.
    struct DocumentSchemaHeader
    {
        DocumentSchemaHeader() = default;
        DocumentSchemaHeader(std::string schemaHeaderString, const Mark& mark) : SchemaHeader(std::move(schemaHeaderString)), Mark(mark) {}

        std::string SchemaHeader;
        Mark Mark;
        static constexpr std::string_view YamlLanguageServerKey = "yaml-language-server";
    };

    struct Document
    {
        Document() = default;
        Document(Node root, DocumentSchemaHeader schemaHeader) : m_root(std::move(root)), m_schemaHeader(std::move(schemaHeader)) {}

        const DocumentSchemaHeader& GetSchemaHeader() const { return m_schemaHeader; }

        // Return r-values for move semantics
        Node&& GetRoot() && { return std::move(m_root); }

    private:
        Node m_root;
        DocumentSchemaHeader m_schemaHeader;
    };

    // Forward declaration to allow pImpl in this Emitter.
    namespace Wrapper
    {
        struct Document;
    }

    // Loads from the input; returns the root node of the first document.
    Document LoadDocument(std::string_view input);
    Document LoadDocument(const std::string& input);
    Document LoadDocument(const std::filesystem::path& input);
    Document LoadDocument(const std::filesystem::path& input, Utility::SHA256::HashBuffer& hashOut);

    // A YAML emitter.
    struct Emitter
    {
        Emitter();

        Emitter(const Emitter&) = delete;
        Emitter& operator=(const Emitter&) = delete;

        Emitter(Emitter&&) noexcept;
        Emitter& operator=(Emitter&&) noexcept;

        ~Emitter();

        // Emit events and values.
        Emitter& operator<<(EmitterEvent event);
        Emitter& operator<<(std::string_view value);
        Emitter& operator<<(int64_t value);
        Emitter& operator<<(int value);
        Emitter& operator<<(bool value);

        Emitter& operator<<(ScalarStyle style);

        // Gets the result of the emitter; can only be retrieved once.
        std::string str();

        // Gets the result of the emitter to out stream; can only be retrieved once.
        void Emit(std::ostream& out);

    private:
        // Appends the given node to the current container if applicable.
        void AppendNode(int id);

        std::unique_ptr<Wrapper::Document> m_document;

        // If set, stores the last Key that was set.
        std::optional<int> m_keyId;

        struct ContainerInfo
        {
            ContainerInfo(int id, bool map) : Id(id), IsMapping(map) {}

            int Id;
            bool IsMapping;
        };

        // The stack of containers being emitted.
        std::stack<ContainerInfo> m_containers;

        // *** State Machine ***

        // The type of input coming into the emitter.
        enum class InputType
        {
            Scalar,
            BeginSeq,
            EndSeq,
            BeginMap,
            EndMap,
            Key,
            Value,
        };

        // If set, defines the type of the next scalar (Key or Value).
        std::optional<InputType> m_scalarType;

        // If set, defines the style of the next scalar.
        std::optional<ScalarStyle> m_scalarStyle;

        // Converts the input type to a bitmask value.
        size_t GetInputBitmask(InputType type);

        // Checks the state of the emitter to ensure that the incoming value is acceptable.
        void CheckInput(InputType type);

        // The currently allowed input types.
        size_t m_allowedInputs = 0;

        template <InputType... types>
        void SetAllowedInputs()
        {
            m_allowedInputs = (GetInputBitmask(types) | ...);
        }

        // Sets the allowed inputs for the container on the top of the stack.
        void SetAllowedInputsForContainer();
    };
}
