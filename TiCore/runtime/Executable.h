/**
 * Appcelerator Titanium License
 * This source code and all modifications done by Appcelerator
 * are licensed under the Apache Public License (version 2) and
 * are Copyright (c) 2009-2012 by Appcelerator, Inc.
 */

/*
 * Copyright (C) 2009, 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef Executable_h
#define Executable_h

#include "CallData.h"
#include "TiFunction.h"
#include "Interpreter.h"
#include "Nodes.h"
#include "SamplingTool.h"
#include <wtf/PassOwnPtr.h>

namespace TI {

    class CodeBlock;
    class Debugger;
    class EvalCodeBlock;
    class FunctionCodeBlock;
    class ProgramCodeBlock;
    class ScopeChainNode;

    struct ExceptionInfo;

    class ExecutableBase : public TiCell {
        friend class JIT;

    protected:
        static const int NUM_PARAMETERS_IS_HOST = 0;
        static const int NUM_PARAMETERS_NOT_COMPILED = -1;
    
    public:
        ExecutableBase(TiGlobalData& globalData, Structure* structure, int numParameters)
            : TiCell(globalData, structure)
            , m_numParametersForCall(numParameters)
            , m_numParametersForConstruct(numParameters)
        {
#if ENABLE(JIT)
            Weak<ExecutableBase> finalizer(globalData, this, executableFinalizer());
            finalizer.leakHandle();
#endif
        }

        bool isHostFunction() const
        {
            ASSERT((m_numParametersForCall == NUM_PARAMETERS_IS_HOST) == (m_numParametersForConstruct == NUM_PARAMETERS_IS_HOST));
            return m_numParametersForCall == NUM_PARAMETERS_IS_HOST;
        }

        static Structure* createStructure(TiGlobalData& globalData, TiValue proto) { return Structure::create(globalData, proto, TypeInfo(CompoundType, StructureFlags), AnonymousSlotCount, &s_info); }
        
        static const ClassInfo s_info;

    protected:
        static const unsigned StructureFlags = 0;
        int m_numParametersForCall;
        int m_numParametersForConstruct;

#if ENABLE(JIT)
    public:
        JITCode& generatedJITCodeForCall()
        {
            ASSERT(m_jitCodeForCall);
            return m_jitCodeForCall;
        }

        JITCode& generatedJITCodeForConstruct()
        {
            ASSERT(m_jitCodeForConstruct);
            return m_jitCodeForConstruct;
        }

        void clearExecutableCode()
        {
            m_jitCodeForCall.clear();
            m_jitCodeForConstruct.clear();
        }

    protected:
        JITCode m_jitCodeForCall;
        JITCode m_jitCodeForConstruct;
        MacroAssemblerCodePtr m_jitCodeForCallWithArityCheck;
        MacroAssemblerCodePtr m_jitCodeForConstructWithArityCheck;
        
    private:
        static WeakHandleOwner* executableFinalizer();
#endif
    };

    class NativeExecutable : public ExecutableBase {
        friend class JIT;
    public:
#if ENABLE(JIT)
        static NativeExecutable* create(TiGlobalData& globalData, MacroAssemblerCodePtr callThunk, NativeFunction function, MacroAssemblerCodePtr constructThunk, NativeFunction constructor)
        {
            if (!callThunk)
                return new (&globalData) NativeExecutable(globalData, JITCode(), function, JITCode(), constructor);
            return new (&globalData) NativeExecutable(globalData, JITCode::HostFunction(callThunk), function, JITCode::HostFunction(constructThunk), constructor);
        }
#else
        static NativeExecutable* create(TiGlobalData& globalData, NativeFunction function, NativeFunction constructor)
        {
            return new (&globalData) NativeExecutable(globalData, function, constructor);
        }
#endif

        ~NativeExecutable();

        NativeFunction function() { return m_function; }

        static Structure* createStructure(TiGlobalData& globalData, TiValue proto) { return Structure::create(globalData, proto, TypeInfo(LeafType, StructureFlags), AnonymousSlotCount, &s_info); }
        
        static const ClassInfo s_info;
    
    private:
#if ENABLE(JIT)
        NativeExecutable(TiGlobalData& globalData, JITCode callThunk, NativeFunction function, JITCode constructThunk, NativeFunction constructor)
            : ExecutableBase(globalData, globalData.nativeExecutableStructure.get(), NUM_PARAMETERS_IS_HOST)
            , m_function(function)
            , m_constructor(constructor)
        {
            m_jitCodeForCall = callThunk;
            m_jitCodeForConstruct = constructThunk;
            m_jitCodeForCallWithArityCheck = callThunk.addressForCall();
            m_jitCodeForConstructWithArityCheck = constructThunk.addressForCall();
        }
#else
        NativeExecutable(TiGlobalData& globalData, NativeFunction function, NativeFunction constructor)
            : ExecutableBase(globalData, globalData.nativeExecutableStructure.get(), NUM_PARAMETERS_IS_HOST)
            , m_function(function)
            , m_constructor(constructor)
        {
        }
#endif

        NativeFunction m_function;
        // Probably should be a NativeConstructor, but this will currently require rewriting the JIT
        // trampoline. It may be easier to make NativeFunction be passed 'this' as a part of the ArgList.
        NativeFunction m_constructor;
    };

    class ScriptExecutable : public ExecutableBase {
    public:
        ScriptExecutable(Structure* structure, TiGlobalData* globalData, const SourceCode& source, bool isInStrictContext)
            : ExecutableBase(*globalData, structure, NUM_PARAMETERS_NOT_COMPILED)
            , m_source(source)
            , m_features(isInStrictContext ? StrictModeFeature : 0)
        {
#if ENABLE(CODEBLOCK_SAMPLING)
            relaxAdoptionRequirement();
            if (SamplingTool* sampler = globalData->interpreter->sampler())
                sampler->notifyOfScope(this);
#else
            UNUSED_PARAM(globalData);
#endif
        }

        ScriptExecutable(Structure* structure, TiExcState* exec, const SourceCode& source, bool isInStrictContext)
            : ExecutableBase(exec->globalData(), structure, NUM_PARAMETERS_NOT_COMPILED)
            , m_source(source)
            , m_features(isInStrictContext ? StrictModeFeature : 0)
        {
#if ENABLE(CODEBLOCK_SAMPLING)
            relaxAdoptionRequirement();
            if (SamplingTool* sampler = exec->globalData().interpreter->sampler())
                sampler->notifyOfScope(this);
#else
            UNUSED_PARAM(exec);
#endif
        }

        const SourceCode& source() { return m_source; }
        intptr_t sourceID() const { return m_source.provider()->asID(); }
        const UString& sourceURL() const { return m_source.provider()->url(); }
        int lineNo() const { return m_firstLine; }
        int lastLine() const { return m_lastLine; }

        bool usesEval() const { return m_features & EvalFeature; }
        bool usesArguments() const { return m_features & ArgumentsFeature; }
        bool needsActivation() const { return m_hasCapturedVariables || m_features & (EvalFeature | WithFeature | CatchFeature); }
        bool isStrictMode() const { return m_features & StrictModeFeature; }

        virtual void unlinkCalls() = 0;
        
        static const ClassInfo s_info;
    protected:
        void recordParse(CodeFeatures features, bool hasCapturedVariables, int firstLine, int lastLine)
        {
            m_features = features;
            m_hasCapturedVariables = hasCapturedVariables;
            m_firstLine = firstLine;
            m_lastLine = lastLine;
        }

        SourceCode m_source;
        CodeFeatures m_features;
        bool m_hasCapturedVariables;
        int m_firstLine;
        int m_lastLine;
    };

    class EvalExecutable : public ScriptExecutable {
    public:

        ~EvalExecutable();

        TiObject* compile(TiExcState* exec, ScopeChainNode* scopeChainNode)
        {
            ASSERT(exec->globalData().dynamicGlobalObject);
            TiObject* error = 0;
            if (!m_evalCodeBlock)
                error = compileInternal(exec, scopeChainNode);
            ASSERT(!error == !!m_evalCodeBlock);
            return error;
        }

        EvalCodeBlock& generatedBytecode()
        {
            ASSERT(m_evalCodeBlock);
            return *m_evalCodeBlock;
        }

        static EvalExecutable* create(TiExcState* exec, const SourceCode& source, bool isInStrictContext) { return new (exec) EvalExecutable(exec, source, isInStrictContext); }

#if ENABLE(JIT)
        JITCode& generatedJITCode()
        {
            return generatedJITCodeForCall();
        }
#endif
        static Structure* createStructure(TiGlobalData& globalData, TiValue proto)
        {
            return Structure::create(globalData, proto, TypeInfo(CompoundType, StructureFlags), AnonymousSlotCount, &s_info);
        }
        
        static const ClassInfo s_info;
    private:
        static const unsigned StructureFlags = OverridesVisitChildren | ScriptExecutable::StructureFlags;
        EvalExecutable(TiExcState*, const SourceCode&, bool);

        TiObject* compileInternal(TiExcState*, ScopeChainNode*);
        virtual void visitChildren(SlotVisitor&);
        void unlinkCalls();

        OwnPtr<EvalCodeBlock> m_evalCodeBlock;
    };

    class ProgramExecutable : public ScriptExecutable {
    public:
        static ProgramExecutable* create(TiExcState* exec, const SourceCode& source)
        {
            return new (exec) ProgramExecutable(exec, source);
        }

        ~ProgramExecutable();

        TiObject* compile(TiExcState* exec, ScopeChainNode* scopeChainNode)
        {
            ASSERT(exec->globalData().dynamicGlobalObject);
            TiObject* error = 0;
            if (!m_programCodeBlock)
                error = compileInternal(exec, scopeChainNode);
            ASSERT(!error == !!m_programCodeBlock);
            return error;
        }

        ProgramCodeBlock& generatedBytecode()
        {
            ASSERT(m_programCodeBlock);
            return *m_programCodeBlock;
        }

        TiObject* checkSyntax(TiExcState*);

#if ENABLE(JIT)
        JITCode& generatedJITCode()
        {
            return generatedJITCodeForCall();
        }
#endif
        
        static Structure* createStructure(TiGlobalData& globalData, TiValue proto)
        {
            return Structure::create(globalData, proto, TypeInfo(CompoundType, StructureFlags), AnonymousSlotCount, &s_info);
        }
        
        static const ClassInfo s_info;

    private:
        static const unsigned StructureFlags = OverridesVisitChildren | ScriptExecutable::StructureFlags;
        ProgramExecutable(TiExcState*, const SourceCode&);

        TiObject* compileInternal(TiExcState*, ScopeChainNode*);
        virtual void visitChildren(SlotVisitor&);
        void unlinkCalls();

        OwnPtr<ProgramCodeBlock> m_programCodeBlock;
    };

    class FunctionExecutable : public ScriptExecutable {
        friend class JIT;
    public:
        static FunctionExecutable* create(TiExcState* exec, const Identifier& name, const SourceCode& source, bool forceUsesArguments, FunctionParameters* parameters, bool isInStrictContext, int firstLine, int lastLine)
        {
            return new (exec) FunctionExecutable(exec, name, source, forceUsesArguments, parameters, isInStrictContext, firstLine, lastLine);
        }

        static FunctionExecutable* create(TiGlobalData* globalData, const Identifier& name, const SourceCode& source, bool forceUsesArguments, FunctionParameters* parameters, bool isInStrictContext, int firstLine, int lastLine)
        {
            return new (globalData) FunctionExecutable(globalData, name, source, forceUsesArguments, parameters, isInStrictContext, firstLine, lastLine);
        }

        TiFunction* make(TiExcState* exec, ScopeChainNode* scopeChain)
        {
            return new (exec) TiFunction(exec, this, scopeChain);
        }
        
        // Returns either call or construct bytecode. This can be appropriate
        // for answering questions that that don't vary between call and construct --
        // for example, argumentsRegister().
        FunctionCodeBlock& generatedBytecode()
        {
            if (m_codeBlockForCall)
                return *m_codeBlockForCall;
            ASSERT(m_codeBlockForConstruct);
            return *m_codeBlockForConstruct;
        }

        TiObject* compileForCall(TiExcState* exec, ScopeChainNode* scopeChainNode)
        {
            ASSERT(exec->globalData().dynamicGlobalObject);
            TiObject* error = 0;
            if (!m_codeBlockForCall)
                error = compileForCallInternal(exec, scopeChainNode);
            ASSERT(!error == !!m_codeBlockForCall);
            return error;
        }

        bool isGeneratedForCall() const
        {
            return m_codeBlockForCall;
        }

        FunctionCodeBlock& generatedBytecodeForCall()
        {
            ASSERT(m_codeBlockForCall);
            return *m_codeBlockForCall;
        }

        TiObject* compileForConstruct(TiExcState* exec, ScopeChainNode* scopeChainNode)
        {
            ASSERT(exec->globalData().dynamicGlobalObject);
            TiObject* error = 0;
            if (!m_codeBlockForConstruct)
                error = compileForConstructInternal(exec, scopeChainNode);
            ASSERT(!error == !!m_codeBlockForConstruct);
            return error;
        }

        bool isGeneratedForConstruct() const
        {
            return m_codeBlockForConstruct;
        }

        FunctionCodeBlock& generatedBytecodeForConstruct()
        {
            ASSERT(m_codeBlockForConstruct);
            return *m_codeBlockForConstruct;
        }

        const Identifier& name() { return m_name; }
        size_t parameterCount() const { return m_parameters->size(); }
        unsigned capturedVariableCount() const { return m_numCapturedVariables; }
        UString paramString() const;
        SharedSymbolTable* symbolTable() const { return m_symbolTable; }

        void discardCode();
        void visitChildren(SlotVisitor&);
        static FunctionExecutable* fromGlobalCode(const Identifier&, TiExcState*, Debugger*, const SourceCode&, TiObject** exception);
        static Structure* createStructure(TiGlobalData& globalData, TiValue proto)
        {
            return Structure::create(globalData, proto, TypeInfo(CompoundType, StructureFlags), AnonymousSlotCount, &s_info);
        }
        
        static const ClassInfo s_info;

    private:
        FunctionExecutable(TiGlobalData*, const Identifier& name, const SourceCode&, bool forceUsesArguments, FunctionParameters*, bool, int firstLine, int lastLine);
        FunctionExecutable(TiExcState*, const Identifier& name, const SourceCode&, bool forceUsesArguments, FunctionParameters*, bool, int firstLine, int lastLine);

        TiObject* compileForCallInternal(TiExcState*, ScopeChainNode*);
        TiObject* compileForConstructInternal(TiExcState*, ScopeChainNode*);
        
        static const unsigned StructureFlags = OverridesVisitChildren | ScriptExecutable::StructureFlags;
        unsigned m_numCapturedVariables : 31;
        bool m_forceUsesArguments : 1;
        void unlinkCalls();

        RefPtr<FunctionParameters> m_parameters;
        OwnPtr<FunctionCodeBlock> m_codeBlockForCall;
        OwnPtr<FunctionCodeBlock> m_codeBlockForConstruct;
        Identifier m_name;
        SharedSymbolTable* m_symbolTable;

#if ENABLE(JIT)
    public:
        MacroAssemblerCodePtr generatedJITCodeForCallWithArityCheck()
        {
            ASSERT(m_jitCodeForCall);
            ASSERT(m_jitCodeForCallWithArityCheck);
            return m_jitCodeForCallWithArityCheck;
        }

        MacroAssemblerCodePtr generatedJITCodeForConstructWithArityCheck()
        {
            ASSERT(m_jitCodeForConstruct);
            ASSERT(m_jitCodeForConstructWithArityCheck);
            return m_jitCodeForConstructWithArityCheck;
        }
#endif
    };

    inline FunctionExecutable* TiFunction::jsExecutable() const
    {
        ASSERT(!isHostFunctionNonInline());
        return static_cast<FunctionExecutable*>(m_executable.get());
    }

    inline bool TiFunction::isHostFunction() const
    {
        ASSERT(m_executable);
        return m_executable->isHostFunction();
    }

    inline NativeFunction TiFunction::nativeFunction()
    {
        ASSERT(isHostFunction());
        return static_cast<NativeExecutable*>(m_executable.get())->function();
    }
}

#endif
