#include "prx/libSceNpCppWebApi/include/Api.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>

static void Require(bool value) { if (!value) std::abort(); }

static constexpr int NotSignedIn = static_cast<int>(0x80552907);

using Object = int (APS5_VABI*)(void*);
using Call = int (APS5_VABI*)();

struct RequestFlow {
    Object transactionConstruct;
    Object transactionDestroy;
    Call setOnFinishedCallback;
    Call setResponseInformationOption;
    Call start;
    Call finish;
    Call getResponseHeaders;
    Object parameterConstruct;
    Object parameterDestroy;
    Call initialize;
    Call terminate;
    Call call;
};

static const RequestFlow flows[] = {
    {
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V118GetFriendsResponseEEENS4_INS2_18ResponseHeaderBaseEEEEC1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V118GetFriendsResponseEEENS4_INS2_18ResponseHeaderBaseEEEED1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V118GetFriendsResponseEEENS4_INS2_18ResponseHeaderBaseEEEE21setOnFinishedCallbackEPFvliS8_PvEPFvliSC_ESC_,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V118GetFriendsResponseEEENS4_INS2_18ResponseHeaderBaseEEEE28setResponseInformationOptionEP37SceNpWebApi2ResponseInformationOption,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V118GetFriendsResponseEEENS4_INS2_18ResponseHeaderBaseEEEE5startEPNS2_10LibContextE,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V118GetFriendsResponseEEENS4_INS2_18ResponseHeaderBaseEEEE6finishEv,
        _ZNK3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V118GetFriendsResponseEEENS4_INS2_18ResponseHeaderBaseEEEE18getResponseHeadersERSA_,
        _ZN3sce2Np9CppWebApi11UserProfile2V110FriendsApi21ParameterToGetFriendsC1Ev,
        _ZN3sce2Np9CppWebApi11UserProfile2V110FriendsApi21ParameterToGetFriendsD1Ev,
        _ZN3sce2Np9CppWebApi11UserProfile2V110FriendsApi21ParameterToGetFriends10initializeEPNS1_6Common10LibContextEPKc,
        _ZN3sce2Np9CppWebApi11UserProfile2V110FriendsApi21ParameterToGetFriends9terminateEv,
        _ZN3sce2Np9CppWebApi11UserProfile2V110FriendsApi10getFriendsEiRKNS4_21ParameterToGetFriendsERNS1_6Common11TransactionINS8_12IntrusivePtrINS3_18GetFriendsResponseEEENSA_INS8_18ResponseHeaderBaseEEEEE,
    },
    {
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V124GetBlockingUsersResponseEEENS4_INS2_18ResponseHeaderBaseEEEEC1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V124GetBlockingUsersResponseEEENS4_INS2_18ResponseHeaderBaseEEEED1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V124GetBlockingUsersResponseEEENS4_INS2_18ResponseHeaderBaseEEEE21setOnFinishedCallbackEPFvliS8_PvEPFvliSC_ESC_,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V124GetBlockingUsersResponseEEENS4_INS2_18ResponseHeaderBaseEEEE28setResponseInformationOptionEP37SceNpWebApi2ResponseInformationOption,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V124GetBlockingUsersResponseEEENS4_INS2_18ResponseHeaderBaseEEEE5startEPNS2_10LibContextE,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V124GetBlockingUsersResponseEEENS4_INS2_18ResponseHeaderBaseEEEE6finishEv,
        _ZNK3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V124GetBlockingUsersResponseEEENS4_INS2_18ResponseHeaderBaseEEEE18getResponseHeadersERSA_,
        _ZN3sce2Np9CppWebApi11UserProfile2V19BlocksApi27ParameterToGetBlockingUsersC1Ev,
        _ZN3sce2Np9CppWebApi11UserProfile2V19BlocksApi27ParameterToGetBlockingUsersD1Ev,
        _ZN3sce2Np9CppWebApi11UserProfile2V19BlocksApi27ParameterToGetBlockingUsers10initializeEPNS1_6Common10LibContextE,
        _ZN3sce2Np9CppWebApi11UserProfile2V19BlocksApi27ParameterToGetBlockingUsers9terminateEv,
        _ZN3sce2Np9CppWebApi11UserProfile2V19BlocksApi16getBlockingUsersEiRKNS4_27ParameterToGetBlockingUsersERNS1_6Common11TransactionINS8_12IntrusivePtrINS3_24GetBlockingUsersResponseEEENSA_INS8_18ResponseHeaderBaseEEEEE,
    },
    {
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V125GetBasicPresencesResponseEEENS4_INS2_18ResponseHeaderBaseEEEEC1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V125GetBasicPresencesResponseEEENS4_INS2_18ResponseHeaderBaseEEEED1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_11UserProfile2V125GetBasicPresencesResponseEEENS4_INS2_18ResponseHeaderBaseEEEE21setOnFinishedCallbackEPFvliS8_PvEPFvliSC_ESC_,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V125GetBasicPresencesResponseEEENS4_INS2_18ResponseHeaderBaseEEEE28setResponseInformationOptionEP37SceNpWebApi2ResponseInformationOption,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V125GetBasicPresencesResponseEEENS4_INS2_18ResponseHeaderBaseEEEE5startEPNS2_10LibContextE,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V125GetBasicPresencesResponseEEENS4_INS2_18ResponseHeaderBaseEEEE6finishEv,
        _ZNK3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_11UserProfile2V125GetBasicPresencesResponseEEENS4_INS2_18ResponseHeaderBaseEEEE18getResponseHeadersERSA_,
        _ZN3sce2Np9CppWebApi11UserProfile2V111PresenceApi28ParameterToGetBasicPresencesC1Ev,
        _ZN3sce2Np9CppWebApi11UserProfile2V111PresenceApi28ParameterToGetBasicPresencesD1Ev,
        _ZN3sce2Np9CppWebApi11UserProfile2V111PresenceApi28ParameterToGetBasicPresences10initializeEPNS1_6Common10LibContextEPKc,
        _ZN3sce2Np9CppWebApi11UserProfile2V111PresenceApi28ParameterToGetBasicPresences9terminateEv,
        _ZN3sce2Np9CppWebApi11UserProfile2V111PresenceApi17getBasicPresencesEiRKNS4_28ParameterToGetBasicPresencesERNS1_6Common11TransactionINS8_12IntrusivePtrINS3_25GetBasicPresencesResponseEEENSA_INS8_18ResponseHeaderBaseEEEEE,
    },
    {
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_15ProfanityFilter2V223FilterProfanityResponseEEENS4_INS2_18ResponseHeaderBaseEEEEC1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_15ProfanityFilter2V223FilterProfanityResponseEEENS4_INS2_18ResponseHeaderBaseEEEED1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_15ProfanityFilter2V223FilterProfanityResponseEEENS4_INS2_18ResponseHeaderBaseEEEE21setOnFinishedCallbackEPFvliS8_PvEPFvliSC_ESC_,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_15ProfanityFilter2V223FilterProfanityResponseEEENS4_INS2_18ResponseHeaderBaseEEEE28setResponseInformationOptionEP37SceNpWebApi2ResponseInformationOption,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_15ProfanityFilter2V223FilterProfanityResponseEEENS4_INS2_18ResponseHeaderBaseEEEE5startEPNS2_10LibContextE,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_15ProfanityFilter2V223FilterProfanityResponseEEENS4_INS2_18ResponseHeaderBaseEEEE6finishEv,
        _ZNK3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_15ProfanityFilter2V223FilterProfanityResponseEEENS4_INS2_18ResponseHeaderBaseEEEE18getResponseHeadersERSA_,
        _ZN3sce2Np9CppWebApi15ProfanityFilter2V212ProfanityApi26ParameterToFilterProfanityC1Ev,
        _ZN3sce2Np9CppWebApi15ProfanityFilter2V212ProfanityApi26ParameterToFilterProfanityD1Ev,
        _ZN3sce2Np9CppWebApi15ProfanityFilter2V212ProfanityApi26ParameterToFilterProfanity10initializeEPNS1_6Common10LibContextEPKcSA_NS6_12IntrusivePtrINS3_19WebApiFilterRequestEEE,
        _ZN3sce2Np9CppWebApi15ProfanityFilter2V212ProfanityApi26ParameterToFilterProfanity9terminateEv,
        _ZN3sce2Np9CppWebApi15ProfanityFilter2V212ProfanityApi15filterProfanityEiRKNS4_26ParameterToFilterProfanityERNS1_6Common11TransactionINS8_12IntrusivePtrINS3_23FilterProfanityResponseEEENSA_INS8_18ResponseHeaderBaseEEEEE,
    },
    {
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_30CommunicationRestrictionStatus2V338CommunicationRestrictionStatusResponseEEENS4_INS2_18ResponseHeaderBaseEEEEC1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_30CommunicationRestrictionStatus2V338CommunicationRestrictionStatusResponseEEENS4_INS2_18ResponseHeaderBaseEEEED1Ev,
        _ZN3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_30CommunicationRestrictionStatus2V338CommunicationRestrictionStatusResponseEEENS4_INS2_18ResponseHeaderBaseEEEE21setOnFinishedCallbackEPFvliS8_PvEPFvliSC_ESC_,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_30CommunicationRestrictionStatus2V338CommunicationRestrictionStatusResponseEEENS4_INS2_18ResponseHeaderBaseEEEE28setResponseInformationOptionEP37SceNpWebApi2ResponseInformationOption,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_30CommunicationRestrictionStatus2V338CommunicationRestrictionStatusResponseEEENS4_INS2_18ResponseHeaderBaseEEEE5startEPNS2_10LibContextE,
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_30CommunicationRestrictionStatus2V338CommunicationRestrictionStatusResponseEEENS4_INS2_18ResponseHeaderBaseEEEE6finishEv,
        _ZNK3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_30CommunicationRestrictionStatus2V338CommunicationRestrictionStatusResponseEEENS4_INS2_18ResponseHeaderBaseEEEE18getResponseHeadersERSA_,
        _ZN3sce2Np9CppWebApi30CommunicationRestrictionStatus2V333CommunicationRestrictionStatusApi44ParameterToGetCommunicationRestrictionStatusC1Ev,
        _ZN3sce2Np9CppWebApi30CommunicationRestrictionStatus2V333CommunicationRestrictionStatusApi44ParameterToGetCommunicationRestrictionStatusD1Ev,
        _ZN3sce2Np9CppWebApi30CommunicationRestrictionStatus2V333CommunicationRestrictionStatusApi44ParameterToGetCommunicationRestrictionStatus10initializeEPNS1_6Common10LibContextEm,
        _ZN3sce2Np9CppWebApi30CommunicationRestrictionStatus2V333CommunicationRestrictionStatusApi44ParameterToGetCommunicationRestrictionStatus9terminateEv,
        _ZN3sce2Np9CppWebApi30CommunicationRestrictionStatus2V333CommunicationRestrictionStatusApi33getCommunicationRestrictionStatusEiRKNS4_44ParameterToGetCommunicationRestrictionStatusERNS1_6Common11TransactionINS8_12IntrusivePtrINS3_38CommunicationRestrictionStatusResponseEEENSA_INS8_18ResponseHeaderBaseEEEEE,
    },
};

static void TestRequestFlow(const RequestFlow& flow) {
    alignas(16) unsigned char transaction[0x100];
    alignas(16) unsigned char parameter[0x100];
    std::memset(transaction, 0x5a, sizeof(transaction));
    std::memset(parameter, 0x5a, sizeof(parameter));

    Require(flow.transactionConstruct(transaction) == 0);
    Require(flow.setOnFinishedCallback() == 0);
    Require(flow.setResponseInformationOption() == 0);
    Require(flow.parameterConstruct(parameter) == 0);
    Require(flow.initialize() == 0);
    Require(flow.call() == NotSignedIn);
    Require(flow.start() == NotSignedIn);
    Require(flow.getResponseHeaders() == NotSignedIn);
    Require(flow.finish() == 0);
    Require(flow.terminate() == 0);
    Require(flow.parameterDestroy(parameter) == 0);
    Require(flow.transactionDestroy(transaction) == 0);
}

using Start = int (APS5_VABI*)(void*, void*);
using GetResponse = int (APS5_VABI*)(const void*, void*);

struct UninstantiatedTransaction {
    Start start;
    GetResponse getResponse;
};

static const UninstantiatedTransaction uninstantiatedTransactions[] = {
    {
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS2_6VectorINS4_INS1_13InGameCatalog2V59ContainerEEEEEEENS4_INS2_18ResponseHeaderBaseEEEE5startEPNS2_10LibContextE,
        _ZNK3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS2_6VectorINS4_INS1_13InGameCatalog2V59ContainerEEEEEEENS4_INS2_18ResponseHeaderBaseEEEE11getResponseERSB_,
    },
    {
        _ZN3sce2Np9CppWebApi6Common15TransactionBaseINS2_12IntrusivePtrINS1_17TitleCloudStorage2V132GetMultiDataStatusesResponseBodyEEENS4_INS2_18ResponseHeaderBaseEEEE5startEPNS2_10LibContextE,
        _ZNK3sce2Np9CppWebApi6Common11TransactionINS2_12IntrusivePtrINS1_17TitleCloudStorage2V132GetMultiDataStatusesResponseBodyEEENS4_INS2_18ResponseHeaderBaseEEEE11getResponseERS8_,
    },
};

static void TestUninstantiatedTransaction(const UninstantiatedTransaction& transaction) {
    alignas(16) unsigned char state[0x100];
    alignas(16) unsigned char response[0x18];
    alignas(16) unsigned char expected[0x18];
    std::uint64_t context = 1;
    std::memset(state, 0x5a, sizeof(state));
    std::memset(response, 0x5a, sizeof(response));
    std::memset(expected, 0x5a, sizeof(expected));

    Require(transaction.start(state, &context) == NotSignedIn);
    Require(transaction.getResponse(state, response) == NotSignedIn);
    Require(std::memcmp(response, expected, sizeof(response)) == 0);
}

int main() {
    for (const auto& flow : flows) TestRequestFlow(flow);
    for (const auto& transaction : uninstantiatedTransactions) TestUninstantiatedTransaction(transaction);
    Require(_ZN3sce2Np9CppWebApi17TitleCloudStorage2V17DataApi26getMultiDataStatusesByUserEiRKNS4_37ParameterToGetMultiDataStatusesByUserERNS1_6Common11TransactionINS8_12IntrusivePtrINS3_32GetMultiDataStatusesResponseBodyEEENSA_INS8_18ResponseHeaderBaseEEEEE() == NotSignedIn);
    Require(_ZN3sce2Np9CppWebApi15ProfanityFilter2V226WebApiFilterRequestFactory6createEPNS1_6Common10LibContextEPKcPNS5_12IntrusivePtrINS3_19WebApiFilterRequestEEE() == NotSignedIn);
    return 0;
}
