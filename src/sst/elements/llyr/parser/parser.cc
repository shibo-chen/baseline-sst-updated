// Copyright 2009-2020 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2020, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>

#include <regex>
#include <iostream>

#include "llvm/Pass.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Metadata.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Demangle/Demangle.h"

#include "parser.h"

namespace SST {
namespace Llyr {

void Parser::generateAppGraph(std::string functionName)
{
    llvm::SMDiagnostic Err;
    llvm::LLVMContext Context;
    std::unique_ptr< llvm::MemoryBuffer > irBuff = llvm::MemoryBuffer::getMemBuffer(offloadString_);

    std::unique_ptr< llvm::Module > mod(llvm::parseIR(irBuff->getMemBufferRef(), Err, Context));

    for( auto functionIter = mod->getFunctionList().begin(), functionEnd = mod->getFunctionList().end(); functionIter != functionEnd; ++functionIter )
    {
        llvm::errs() << "Function Name: ";
        llvm::errs().write_escaped(functionIter->getName()) << "     ";
        llvm::errs().write_escaped(llvm::demangle(functionIter->getName().str() )) << '\n';

        //check each located function to see if it's the offload target
        if( functionIter->getName().find(functionName) != std::string::npos ) {
            generatebBasicBlockGraph(&*functionIter);
            doTheseThings(&*functionIter);

            break;
        }
    }// function loop

    std::cout << "Finished parsing..." << std::endl;

}// generateAppGraph

void Parser::generatebBasicBlockGraph(llvm::Function* func)
{
    std::cout << "Generating BB Graph..." << std::endl;
    llvm::errs().write_escaped(llvm::demangle(func->getName().str() )) << '\n';

    for( auto blockIter = func->getBasicBlockList().begin(), blockEnd = func->getBasicBlockList().end(); blockIter != blockEnd; ++blockIter )
    {
        llvm::Instruction *Inst = llvm::dyn_cast<llvm::Instruction>(blockIter->getTerminator());

        llvm::errs() << "\t+++Basic Block Name(" << &*blockIter << "): ";
        llvm::errs().write_escaped(blockIter->getName()) << "  --->  " << &*Inst << '\n';

        bbGraph_->addVertex(&*blockIter);
    }

    std::map< uint32_t, Vertex< llvm::BasicBlock* > >* vertex_map_ = bbGraph_->getVertexMap();
    typename std::map< uint32_t, Vertex< llvm::BasicBlock* > >::iterator vertexIterator;
    for(vertexIterator = vertex_map_->begin(); vertexIterator != vertex_map_->end(); ++vertexIterator) {
        llvm::errs() << "\nBasic Block " << vertexIterator->second.getValue() << "\n";

        uint32_t totalSuccessors = vertexIterator->second.getValue()->getTerminator()->getNumSuccessors();
        for( uint32_t successor = 0; successor < totalSuccessors; successor++ ) {
            llvm::errs() << "\tSuccessors " << successor << " of " << totalSuccessors << "\n";

            llvm::BasicBlock* tempBB = vertexIterator->second.getValue()->getTerminator()->getSuccessor(successor);
            llvm::errs() << "\nBasic Block " << tempBB << "\n";

            typename std::map< uint32_t, Vertex< llvm::BasicBlock* > >::iterator vertexIteratorInner;
            for(vertexIteratorInner = vertex_map_->begin(); vertexIteratorInner != vertex_map_->end(); ++vertexIteratorInner) {
                if( vertexIteratorInner->second.getValue() == tempBB ) {
                    llvm::errs() << "\t\tFound:  " << vertexIteratorInner->second.getValue() << "\n";

                    bbGraph_->addEdge(vertexIterator->first, vertexIteratorInner->first);

                    break;
                }
            }
        }// successor loop
    }// basic block loop

    // bb_Graph should be complete here
    std::cout << "...Basic Block Graph Done." << std::endl;
    bbGraph_->printDot("00_test.dot");
}// generatebBasicBlockGraph


void Parser::doTheseThings(llvm::Function* func)
{
    std::cout << "\n\nGenerating Other Graph..." << std::endl;
    CDFGVertex* entryVertex;
    CDFGVertex* outputVertex;
    CDFGVertex* inputVertex;
    instructionMap_ = new std::map< llvm::Instruction*, CDFGVertex* >;

    uint32_t tempOpcode;
    for( auto blockIter = func->getBasicBlockList().begin(), blockEnd = func->getBasicBlockList().end(); blockIter != blockEnd; ++blockIter ) {
        (*flowGraph_)[&*blockIter] = new CDFG;
        CDFG &g = *((*flowGraph_)[&*blockIter]);

        (*useNode_)[&*blockIter] = new std::map< CDFGVertex*, std::vector< llvm::Instruction* >* >;
        (*defNode_)[&*blockIter] = new std::map< CDFGVertex*, std::vector< llvm::Instruction* >* >;

        llvm::errs() << "\t+++Basic Block Name(" << &*blockIter << "): ";
        llvm::errs().write_escaped(blockIter->getName()) << '\n';

        for( auto instructionIter = blockIter->begin(), instructionEnd = blockIter->end(); instructionIter != instructionEnd; ++instructionIter ) {
            tempOpcode = instructionIter->getOpcode();

            llvm::errs() << "\t\t**(" << &*instructionIter << ")  " << *instructionIter   << "  --  ";
            llvm::errs() << "Opcode Name:  ";
            llvm::errs().write_escaped(instructionIter->getName()) << "  ";
            llvm::errs().write_escaped(std::to_string(instructionIter->getOpcode())) << "\n";

            outputVertex = new CDFGVertex;
            std::string tutu;
            llvm::raw_string_ostream rso(tutu);
            instructionIter->print(rso);
            outputVertex->instructionName_ = rso.str();
            outputVertex->instruction_ = &*instructionIter;
            outputVertex->intConst_ = 0x00;
            outputVertex->floatConst_ = 0x00;
            outputVertex->doubleConst_ = 0x00;

            uint32_t outputVertexID = g.addVertex(outputVertex);
            (*vertexList_)[&*blockIter].push_back(outputVertex);

            if( g.numVertices() == 1 )
                entryVertex = outputVertex;

            instructionMap_->insert( std::pair< llvm::Instruction*, CDFGVertex* >(&*instructionIter, outputVertex) );

            llvm::errs() << "-------------------------------------------- Users List --------------------------------------------\n";
            for( llvm::User *U : instructionIter->users() )
            {
                if( llvm::Instruction *Inst = llvm::dyn_cast<llvm::Instruction>(U) )
                {
                    llvm::errs() << *instructionIter << " is used in instruction:\t";
                    llvm::errs() << "(" << &*Inst << ") " << *Inst << "\n";
                }

            }
            llvm::errs() << "----------------------------------------------------------------------------------------------------\n";

            //determine operation
            if( tempOpcode == llvm::Instruction::Alloca ) {                                     // BEGIN Allocate

                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                // create the node/use entries (this should be empty)
                (*useNode_)[&*blockIter]->insert( std::pair< CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair< CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ ) {
                    CDFGVertex* tempVal = g.getVertex(outputVertexID)->getValue();
                    llvm::errs() << "Node-Use Entry (" << tempVal->instruction_ << "):  " << *nodeUseEntry << "\n";
                }

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ ) {
                    CDFGVertex* tempVal = g.getVertex(outputVertexID)->getValue();
                    llvm::errs() << "Node-Def Entry (" << tempVal->instruction_ << "):  " << *nodeDefEntry << "\n";
                }

            // END Allocate
            } else if( tempOpcode == llvm::Instruction::Ret ) {                                     // BEGIN Return
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;

                llvm::Value* tempOperand = llvm::cast<llvm::ReturnInst>(instructionIter)->getReturnValue();

                // Test for ret val
                // If a function returns void, the value returned is a null pointer
                if( tempOperand == 0x00) {
                    // create the node/use entries
                    (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, new std::vector< llvm::Instruction* >) );

                    //create the node/def entries
                    (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, new std::vector< llvm::Instruction* >) );
                } else if( llvm::isa<llvm::Constant>(tempOperand) || llvm::isa<llvm::Argument>(tempOperand) || llvm::isa<llvm::GlobalValue>(tempOperand) ) {
                    // create the node/use entries
                    (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, new std::vector< llvm::Instruction* >) );

                    //create the node/def entries
                    (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, new std::vector< llvm::Instruction* >) );
                } else if( llvm::isa<llvm::Instruction>(tempOperand) ) {
                    std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempOperand));
                    if( it != instructionMap_->end() ) {
                        inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempOperand));

                        #ifdef DEBUG
                        llvm::errs() << "+Found " << inputVertex->instruction_ << " in instructionMap_\n";
                        #endif
                    } else {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }

                    //add variable to node use list
                    tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempOperand));

                    // create the node/use entries
                    (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                    //create the node/def entries
                    (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, new std::vector< llvm::Instruction* >) );
                }

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            // END Return
            } else if( tempOpcode == llvm::Instruction::Call ) {                                      // BEGIN Call

                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                for( auto operandIter = instructionIter->op_begin(), operandEnd = instructionIter->op_end(); operandIter != operandEnd; ++operandIter ) {
                    llvm::Value* tempOperand = operandIter->get();

                    if( llvm::isa<llvm::Constant>(tempOperand) ) {
                    // Don't care about these args at the moment

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                    } else if( llvm::isa<llvm::Instruction>(tempOperand) ) {
                        std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempOperand));

                        #ifdef DEBUG
                        llvm::errs() << "**** " << static_cast<llvm::Instruction*>(operandIter->get()) << "   --   " << llvm::cast<llvm::Instruction>(tempOperand) << "   --   ";
                        #endif


                        if( it != instructionMap_->end() ) {
                            inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempOperand));

                            #ifdef DEBUG
                            llvm::errs() << "+Found " << inputVertex->instruction_ << " in instructionMap_\n";
                            #endif
                        } else {
                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }

                        //add variable to node use list
                        tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempOperand));
                    }
                }

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            //END Call
            } else if( tempOpcode == llvm::Instruction::Br ) {                                        // BEGIN Branch
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                if(llvm::cast<llvm::BranchInst>(instructionIter)->isConditional() ) {
                    llvm::Value* tempCond =llvm::cast<llvm::BranchInst>(instructionIter)->getCondition();

                    std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempCond));
                    if( it != instructionMap_->end() ) {
                        inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempCond));

                        #ifdef DEBUG
                        llvm::errs() << "+src Found " << inputVertex->instruction_ << " in instructionMap_\n";
                        #endif
                    } else {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }

                    //add variable to node use list
                    tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempCond));
                }

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            // END Branch
            } else if( tempOpcode == llvm::Instruction::Load ) {                                       // BEGIN Load
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                llvm::Value* tempSrc = llvm::cast<llvm::LoadInst>(instructionIter)->getPointerOperand();
                uint32_t alignment = llvm::cast<llvm::LoadInst>(instructionIter)->getAlignment();

                //Get src information
                if( llvm::isa<llvm::Instruction>(tempSrc) ) {
                    std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempSrc));
                    if( it != instructionMap_->end() ) {
                        inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempSrc));

                        #ifdef DEBUG
                        llvm::errs() << "+src Found " << inputVertex->instruction_ << " in instructionMap_\n";
                        #endif
                    } else {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = alignment;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }

                    //add variable to node use list
                    tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempSrc));
                } else {
                    //TODO fix arguments as source
                    inputVertex = new CDFGVertex;
                    inputVertex->instruction_ = 0x00;
                    inputVertex->intConst_ = alignment;
                    inputVertex->floatConst_ = 0x00;
                    inputVertex->doubleConst_ = 0x00;
                    inputVertex->valueName_ = tempSrc->getName().str();

                    uint32_t inputVertexID = g.addVertex(inputVertex);

                    g.addEdge(inputVertexID, outputVertexID);
//                     if(inserted)
//                     {
//                         g[edgeDesc].value_t = tempSrc;
//                     }

                    (*vertexList_)[&*blockIter].push_back(inputVertex);

                    // create the node/use entries
                    (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                    //create the node/def entries
                    (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                }//end src

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            //END Load
            } else if( tempOpcode == llvm::Instruction::Store ) {                                     // BEGIN Store
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                llvm::Value* tempDst = llvm::cast<llvm::StoreInst>(instructionIter)->getPointerOperand();
                llvm::Value* tempSrc = llvm::cast<llvm::StoreInst>(instructionIter)->getValueOperand();

                //Get destination dependency
                if( llvm::isa<llvm::Instruction>(tempDst) ) {
                    std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempDst));
                    if( it != instructionMap_->end() ) {
                        inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempDst));

                        #ifdef DEBUG
                        llvm::errs() << "+dst Found " << inputVertex->instruction_ << " in instructionMap_\n";
                        #endif
                    } else {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }

                    //add variable to node use list
                    tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempDst));

                } else if( llvm::isa<llvm::Argument>(tempDst) ) {
                    inputVertex = new CDFGVertex;
                    inputVertex->instruction_ = 0x00;
                    inputVertex->intConst_ = 0xFF;
                    inputVertex->floatConst_ = 0x00;
                    inputVertex->doubleConst_ = 0x00;
                    inputVertex->valueName_ = tempSrc->getName().str();

                    uint32_t inputVertexID = g.addVertex(inputVertex);
                    ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                    edgeProp->value_ = llvm::cast<llvm::StoreInst>(instructionIter)->getValueOperand();
                    g.addEdge(inputVertexID, outputVertexID, edgeProp);

                    (*vertexList_)[&*blockIter].push_back(inputVertex);

                    // create the node/use entries
                    (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                    //create the node/def entries
                    (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                } else {
                    if( llvm::isa<llvm::ConstantInt>(tempDst) ) {                           // signed/unsigned ints
                        llvm::ConstantInt* tempConst = llvm::cast<llvm::ConstantInt>(tempDst);

                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = tempConst->getSExtValue();
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // Insert edge for const here since we can't discover it when we walk the graph
                        ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                        edgeProp->value_ = 0x00;
                        g.addEdge(inputVertexID, outputVertexID, edgeProp);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                    } else if( llvm::isa<llvm::ConstantFP>(tempDst) ) {                          // floats and doubles
                        llvm::ConstantFP* tempConst = llvm::cast<llvm::ConstantFP>(tempDst);

                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
                        if(tempDst->getType()->isFloatTy()) {
                            inputVertex->doubleConst_ = (double) tempConst->getValueAPF().convertToFloat();
                        } else {
                            inputVertex->doubleConst_ = tempConst->getValueAPF().convertToDouble();
                        }

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // Insert edge for const here since we can't discover it when we walk the graph
                        ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                        edgeProp->value_ = 0x00;
                        g.addEdge(inputVertexID, outputVertexID, edgeProp);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    } else {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // Insert edge for const here since we can't discover it when we walk the graph
                        ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                        edgeProp->value_ = 0x00;
                        g.addEdge(inputVertexID, outputVertexID, edgeProp);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }

                }// END dst dep check

                //Get source dependency
                if( llvm::isa<llvm::Instruction>(tempSrc) ) {
                    std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempSrc));
                    if( it != instructionMap_->end() ) {
                        inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempSrc));

                        #ifdef DEBUG
                        llvm::errs() << "+src Found " << inputVertex->instruction_ << " in instructionMap_\n";
                        #endif
                    } else {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }

                    //add variable to node use list
                    tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempSrc));
                } else if( llvm::isa<llvm::Argument>(tempSrc) ) {
                    inputVertex = new CDFGVertex;
                    inputVertex->instruction_ = 0x00;
                    inputVertex->intConst_ = 0xFF;
                    inputVertex->floatConst_ = 0x00;
                    inputVertex->doubleConst_ = 0x00;
                    inputVertex->valueName_ = tempSrc->getName().str();

                    uint32_t inputVertexID = g.addVertex(inputVertex);
                    (*vertexList_)[&*blockIter].push_back(inputVertex);

                    ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                    edgeProp->value_ = llvm::cast<llvm::StoreInst>(instructionIter)->getValueOperand();
                    g.addEdge(inputVertexID, outputVertexID, edgeProp);

                    // create the node/use entries
                    (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                    //create the node/def entries
                    (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                } else {
                    if( llvm::isa<llvm::ConstantInt>(tempSrc) ) {                             // signed/unsigned ints
                        llvm::ConstantInt* tempConst = llvm::cast<llvm::ConstantInt>(tempSrc);

                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = tempConst->getSExtValue();
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // Insert edge for const here since we can't discover it when we walk the graph
                        ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                        edgeProp->value_ = 0x00;
                        g.addEdge(inputVertexID, outputVertexID, edgeProp);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                    } else if( llvm::isa<llvm::ConstantFP>(tempSrc) ) {                         // floats and doubles
                        llvm::ConstantFP* tempConst = llvm::cast<llvm::ConstantFP>(tempSrc);

                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
    //                         if(tempSrc->getType()->isFloatTy())
    //                            inputVertex->floatConst = tempConst->getValueAPF().convertToFloat();
    //                         else
                            inputVertex->doubleConst_ = tempConst->getValueAPF().convertToDouble();

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // Insert edge for const here since we can't discover it when we walk the graph
                        ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                        edgeProp->value_ = 0x00;
                        g.addEdge(inputVertexID, outputVertexID, edgeProp);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    } else {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0x00;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        // Insert edge for const here since we can't discover it when we walk the graph
                        ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                        edgeProp->value_ = 0x00;
                        g.addEdge(inputVertexID, outputVertexID, edgeProp);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }

                }// END src dep check

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif
            //END Store
            } else if( tempOpcode == llvm::Instruction::GetElementPtr ){                                     // BEGIN GEP
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                for( auto operandIter = instructionIter->op_begin(), operandEnd = instructionIter->op_end(); operandIter != operandEnd; ++operandIter ) {
                    llvm::Value* tempOperand = operandIter->get();

                    if( llvm::isa<llvm::Constant>(tempOperand) ) {
                        if( llvm::isa<llvm::ConstantInt>(tempOperand) ) {                             // signed/unsigned ints
                            llvm::ConstantInt* tempConst = llvm::cast<llvm::ConstantInt>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = tempConst->getSExtValue();
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        } else if( llvm::isa<llvm::ConstantFP>(tempOperand) ) {                         // floats and doubles
                            llvm::ConstantFP* tempConst = llvm::cast<llvm::ConstantFP>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
    //                            if(tempOperand->getType()->isFloatTy())
    //                               inputVertex->floatConst = tempConst->getValueAPF().convertToFloat();
    //                            else
                                inputVertex->doubleConst_ = tempConst->getValueAPF().convertToDouble();

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }

                    } else if( llvm::isa<llvm::Instruction>(tempOperand) ) {
                        std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempOperand));

                        #ifdef DEBUG
                        llvm::errs() << "**** " << static_cast<llvm::Instruction*>(operandIter->get()) << "   --   " <<
                        llvm::cast<llvm::Instruction>(tempOperand) << "   --   ";
                        #endif

                        if( it != instructionMap_->end() ) {
                            inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempOperand));

                            #ifdef DEBUG
                            llvm::errs() << "+Found " << inputVertex->instruction_ << " in instructionMap_\n";
                            #endif
                        } else {
                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }

                        //add variable to node use list
                        tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempOperand));
                    }
                }

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            //END GEP
            } else if( tempOpcode == llvm::Instruction::ICmp || tempOpcode == llvm::Instruction::FCmp ) {   // BEGIN Int/Float Compare
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                for( auto operandIter = instructionIter->op_begin(), operandEnd = instructionIter->op_end(); operandIter != operandEnd; ++operandIter ) {
                    llvm::Value* tempOperand = operandIter->get();

                    if( llvm::isa<llvm::Constant>(tempOperand) ) {
                        if( llvm::isa<llvm::ConstantInt>(tempOperand) ) {                              // signed/unsigned ints
                            llvm::ConstantInt* tempConst = llvm::cast<llvm::ConstantInt>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = tempConst->getSExtValue();
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        } else if( llvm::isa<llvm::ConstantFP>(tempOperand) ) {                          // floats and doubles
                            llvm::ConstantFP* tempConst = llvm::cast<llvm::ConstantFP>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
    //                            if(tempOperand->getType()->isFloatTy())
    //                               inputVertex->floatConst = tempConst->getValueAPF().convertToFloat();
    //                            else
                            inputVertex->doubleConst_ = tempConst->getValueAPF().convertToDouble();

                                uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }
                    } else if( llvm::isa<llvm::Instruction>(tempOperand) ) {
                        std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempOperand));

                        #ifdef DEBUG
                        llvm::errs() << "**** " << static_cast<llvm::Instruction*>(operandIter->get()) << "   --   " << llvm::cast<llvm::Instruction>(tempOperand) << "   --   ";
                        #endif

                        if( it != instructionMap_->end() ) {
                            inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempOperand));

                            #ifdef DEBUG
                            llvm::errs() << "+Found " << inputVertex->instruction_ << " in instructionMap_\n";
                            #endif
                        } else {
                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }

                        //add variable to node use list
                        tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempOperand));
                    } else if( llvm::isa<llvm::Argument>(tempOperand) || llvm::isa<llvm::GlobalValue>(tempOperand) ) {
                        inputVertex = new CDFGVertex;
                        inputVertex->instruction_ = 0x00;
                        inputVertex->intConst_ = 0xFF;
                        inputVertex->floatConst_ = 0x00;
                        inputVertex->doubleConst_ = 0x00;

                        uint32_t inputVertexID = g.addVertex(inputVertex);
                        (*vertexList_)[&*blockIter].push_back(inputVertex);

                        ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                        edgeProp->value_ = 0x00;
                        g.addEdge(inputVertexID, outputVertexID, edgeProp);

                        // create the node/use entries
                        (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        //create the node/def entries
                        (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                    }
                }

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            // END Int/Float Compare
            } else if( tempOpcode > 35 && tempOpcode < 49 ) {                                   // BEGINllvm::cast operators 36-48
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                for( auto operandIter = instructionIter->op_begin(), operandEnd = instructionIter->op_end(); operandIter != operandEnd; ++operandIter ) {
                    llvm::Value* tempOperand = operandIter->get();

                    if( llvm::isa<llvm::Constant>(tempOperand) ) {
                        if( llvm::isa<llvm::ConstantInt>(tempOperand) ) {                              // signed/unsigned ints
                            llvm::ConstantInt* tempConst = llvm::cast<llvm::ConstantInt>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = tempConst->getSExtValue();
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        }
                        else if( llvm::isa<llvm::ConstantFP>(tempOperand) )                           // floats and doubles
                        {
                            llvm::ConstantFP* tempConst = llvm::cast<llvm::ConstantFP>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
    //                            if(tempOperand->getType()->isFloatTy())
    //                               inputVertex->floatConst = tempConst->getValueAPF().convertToFloat();
    //                            else
                            inputVertex->doubleConst_ = tempConst->getValueAPF().convertToDouble();

                                uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }

                    } else if( llvm::isa<llvm::Instruction>(tempOperand) ) {
                        std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempOperand));

                        #ifdef DEBUG
                        llvm::errs() << "**** " << static_cast<llvm::Instruction*>(operandIter->get()) << "   --   " << llvm::cast<llvm::Instruction>(tempOperand) << "   --   ";
                        #endif

                        if( it != instructionMap_->end() ) {
                            inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempOperand));

                            #ifdef DEBUG
                            llvm::errs() << "+Found " << inputVertex->instruction_ << " in instructionMap_\n";
                            #endif

                        } else {
                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }

                        //add variable to node use list
                        tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempOperand));
                    }

                }

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            //END CAST
            } else if( tempOpcode > 10 && tempOpcode < 29 ) {                // BEGIN binary operators 11-22, logical operators 23-28 -- two operands
                std::vector< llvm::Instruction* > *tempUseVector = new std::vector< llvm::Instruction* >;
                std::vector< llvm::Instruction* > *tempDefVector = new std::vector< llvm::Instruction* >;

                for( auto operandIter = instructionIter->op_begin(), operandEnd = instructionIter->op_end(); operandIter != operandEnd; ++operandIter ) {
                    llvm::Value* tempOperand = operandIter->get();

                    if( llvm::isa<llvm::Constant>(tempOperand) ) {
                        if( llvm::isa<llvm::ConstantInt>(tempOperand) ) {                              // signed/unsigned ints
                            llvm::ConstantInt* tempConst = llvm::cast<llvm::ConstantInt>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = tempConst->getSExtValue();
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                        } else if( llvm::isa<llvm::ConstantFP>(tempOperand) ){                           // floats and doubles
                            llvm::ConstantFP* tempConst = llvm::cast<llvm::ConstantFP>(tempOperand);

                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
    //                            if(tempOperand->getType()->isFloatTy())
    //                               inputVertex->floatConst = tempConst->getValueAPF().convertToFloat();
    //                            else
                            inputVertex->doubleConst_ = tempConst->getValueAPF().convertToDouble();

                                uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // Insert edge for const here since we can't discover it when we walk the graph
                            ParserEdgeProperties* edgeProp = new ParserEdgeProperties;
                            edgeProp->value_ = 0x00;
                            g.addEdge(inputVertexID, outputVertexID, edgeProp);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }
                    } else if( llvm::isa<llvm::Instruction>(tempOperand) ) {
                        std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->find(llvm::cast<llvm::Instruction>(tempOperand));

                        #ifdef DEBUG
                        llvm::errs() << "**** " << static_cast<llvm::Instruction*>(operandIter->get()) << "   --   " << llvm::cast<llvm::Instruction>(tempOperand) << "   --   ";
                        #endif

                        if( it != instructionMap_->end() ) {
                            inputVertex = instructionMap_->at(llvm::cast<llvm::Instruction>(tempOperand));

                            #ifdef DEBUG
                            llvm::errs() << "+Found " << inputVertex->instruction_ << " in instructionMap_\n";
                            #endif
                        } else {
                            inputVertex = new CDFGVertex;
                            inputVertex->instruction_ = 0x00;
                            inputVertex->intConst_ = 0x00;
                            inputVertex->floatConst_ = 0x00;
                            inputVertex->doubleConst_ = 0x00;

                            uint32_t inputVertexID = g.addVertex(inputVertex);
                            (*vertexList_)[&*blockIter].push_back(inputVertex);

                            // create the node/use entries
                            (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );

                            //create the node/def entries
                            (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(inputVertex, new std::vector< llvm::Instruction* >) );
                        }

                        //add variable to node use list
                        tempUseVector->push_back(llvm::cast<llvm::Instruction>(tempOperand));
                    }

                }

                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempUseVector) );

                //create the node/def entries
                tempDefVector->push_back(&*instructionIter);
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, tempDefVector) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            //END ALU
            } else {
                // create the node/use entries
                (*useNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, new std::vector< llvm::Instruction* >) );

                //create the node/def entries
                (*defNode_)[&*blockIter]->insert( std::pair<CDFGVertex*, std::vector< llvm::Instruction* >* >(outputVertex, new std::vector< llvm::Instruction* >) );

                #ifdef DEBUG
                for( auto nodeUseEntry = (*useNode_)[&*blockIter]->at(outputVertex)->begin(); nodeUseEntry != (*useNode_)[&*blockIter]->at(outputVertex)->end(); nodeUseEntry++ )
                    llvm::errs() << "Node-Use Entry (" << outputVertex->instruction_ << "):  " << *nodeUseEntry << "\n";

                for( auto nodeDefEntry = (*defNode_)[&*blockIter]->at(outputVertex)->begin(); nodeDefEntry != (*defNode_)[&*blockIter]->at(outputVertex)->end(); nodeDefEntry++ )
                    llvm::errs() << "Node-Def Entry (" << outputVertex->instruction_ << "):  " << *nodeDefEntry << "\n";
                #endif

            }

            #ifdef DEBUG
            llvm::errs() <<   "********************************************* Ins Map  *********************************************\n";
            for( std::map< llvm::Instruction*,CDFGVertex* >::iterator it = instructionMap_->begin(); it != instructionMap_->end(); it++ )
            {
                llvm::errs() << it->first;
                llvm::errs() << "  ";
            }
            llvm::errs() << "\n****************************************************************************************************\n";

            llvm::errs() << "\t\t\tnum operands " << instructionIter->getNumOperands() << "\n";
            for( auto operandIter = instructionIter->op_begin(), operandEnd = instructionIter->op_end(); operandIter != operandEnd; ++operandIter )
            {
                llvm::errs() << "\t\t\top get  " << operandIter->get() << "\n";
                llvm::errs() << "\t\t\top uses " << operandIter->get()->getNumUses() << "\n";
                llvm::errs() << "\t\t\top dump ";
                operandIter->get()->dump();
                if( operandIter->get()->hasName() == 1 )
                {
                    llvm::errs() << "\t\t\tfound  ";
                    llvm::errs().write_escaped(operandIter->get()->getName().str()) << "  ";
                }
                else
                {
                    llvm::errs() << "\t\t\tempty \n";
                }
                llvm::errs() << "\n";
            }
            llvm::errs() << "\n";
            #endif


        }
    }

    // should be complete here
    std::cout << "...Other Graph Done." << std::endl;
//     bbGraph_->printDot("00_test.dot");
}// doTheseThings

} // namespace llyr
} // namespace SST


