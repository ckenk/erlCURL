-module(libcurl_test).


%% Include files


%% Exported Functions
-compile(export_all).


-define('DRIVER_NAME', 'libcurl_drv').
-define(CMD_HTTP_GET, 1).
-define(CMD_HTTP_POST, 2).
-define(CMD_HTTP_PUT, 3).
-define(CMD_HTTP_PATCH, 4).
-define(CMD_HTTP_DELETE, 5).


%% libcurl_test:curl().

%% API Functions
curl() ->	
	JSONBin = "{\"modules\":{\"BCM\":[{\"error\":\"B1609-15\"}],\"PCM\":[{\"error\":\"P1302-00\"}]},\"meta\":{\"user_id\":\"MCDA_RH\"},\"vin\":\"123456789abcdef37\",\"test_type\":\"beta\"}",
	Options = [
			   {erl_drv_debug,"false"}
			   ,{erl_drv_log, "C:/temp/curl_drv.log"}
			   ,{method,"POST"}
			   ,{header,"Authorization:Bearer b32118c0364411eaa06f0242ac110005"}
			   ,{header,"Content-Type: application/json"}
			   ,{body_data,JSONBin}
			   ,{verbose,"-v"}
			  ],
	curl(<some_uri>, Options).


curl(Url, Options) ->
	JSONBin = erlang:list_to_binary((options_to_args(Options ++ [{url,Url}])),
	error_logger:info_msg("JSONBin = ~p~n",[JSONBin]),

	SearchDir = code:priv_dir(proxy),
    case erl_ddll:load(SearchDir, atom_to_list(?DRIVER_NAME)) of
		ok ->
			error_logger:info_msg("Load success"),
			Port = open_port({spawn, atom_to_list (?DRIVER_NAME)}, [stderr_to_stdout,binary]),
			error_logger:info_msg("Port received: ~p~n",[Port]),
			port_control(Port, ?CMD_HTTP_POST, JSONBin),
			error_logger:info_msg("Waiting on ERL Response"),
			Response = wait_result(Port),
			error_logger:info_msg("ERL Response = ~p~n",[Response]),
			
			erlang:port_close(Port),
			erl_ddll:unload(atom_to_list(?DRIVER_NAME));
		
      {error, Error} ->
		  error_logger:info_msg("Error = ~p~n",[Error]),
		  Msg = io_lib:format("Error loading ~s/~p: ~p", [SearchDir, ?DRIVER_NAME, erl_ddll:format_error(Error)]),
		  {stop, lists:flatten (Msg)}
    end.


%% ================================================================
%% Local Functions
%% ================================================================
%% Return a proplist | {error,Error}
wait_result(Port) ->
  receive
	  {Port, Reply} ->
          error_logger:info_msg("Resp: ~p~n", [{Port, Reply}]),
          Reply;
      {error, Reason} ->
          error_logger:info_msg("Error: ~p~n", [Reason]),
          {error, Reason};
	  Response -> 
		  error_logger:info_msg("Response = ~p~n",[Response]),
		  throw({unexpected_response,Response})
  end.


default_args(wincurl) ->
	["-s","-S","-m","30"].


%% JSON1 = lists:flatten(io_lib:format("~s",[common_mochijson2:encode({struct,[{<<"urls">>,{array,[<<"URL1">>,<<"URL2">>]}}]})])).  => "{\"urls\":[\"URL1\",\"URL2\"]}"
%% JSON2 = lists:flatten(io_lib:format("~s",[common_mochijson2:encode({struct,[{<<"-H">>,[<<"H1">>,<<"H2">>]}]})])).				=> "{\"-H\":[\"H1\",\"H2\"]}"
%% --------
%% A = {struct,[{<<"urls">>,{array,[<<"URL1">>,<<"URL2">>]}}]}.
%% B = {struct,[{<<"-H">>,[<<"H1">>,<<"H2">>]}]}.
%% JSON = lists:flatten(io_lib:format("~s",[common_mochijson2:encode({struct,[{<<"curl_params">>,[A,B]}]})])). => "{\"curl_params\":[{\"urls\":[\"URL1\",\"URL2\"]},{\"-H\":[\"H1\",\"H2\"]}]}"
options_to_args(Options) ->	
	ParamsDict = lists:foldl(fun (Option, Dict) -> encode_option(Option, Dict) end, dict:new(), Options),
	%error_logger:info_msg("ParamsDict = ~p~n",[ParamsDict]),
	Keys = dict:fetch_keys(ParamsDict),
	CurlArgList = lists:foldl(fun(Key, Acc) -> 
						Values = dict:fetch(Key, ParamsDict),
						%error_logger:info_msg("Key: ~p, Values = ~p~n",[Key, Values]),
						JSONArrayStruct = {Key, Values},
						%error_logger:info_msg("JSONArrayStruct = ~p~n",[JSONArrayStruct]),
						[JSONArrayStruct | Acc] 
				end, [], Keys),
	error_logger:info_msg("CurlArgList = ~p~n",[CurlArgList]),
	lists:flatten(io_lib:format("~s",[common_mochijson2:encode({struct,CurlArgList})])).


encode_option({verbose,Verbose}, Dict) ->
	dict:append(<<"-v">>, erlang:list_to_binary((Verbose), Dict);

encode_option({url,URL}, Dict) ->
	dict:append(<<"url">>, erlang:list_to_binary((URL), Dict);

encode_option({erl_drv_debug,Bool}, Dict) ->
	dict:append(<<"erl_drv_debug">>, erlang:list_to_binary((Bool), Dict);

encode_option({method,Method}, Dict) ->
	dict:append(<<"--request">>, erlang:list_to_binary((Method), Dict);
encode_option({timeout,Seconds}, Dict) ->
	dict:append(<<"-m">>, erlang:list_to_binary((integer_to_list(Seconds)), Dict);
encode_option({certificate,Certificate}, Dict) ->
	dict:append(<<"-E">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s",[Certificate]))), Dict);
encode_option({certificate,Certificate,Password}, Dict) ->
	dict:append(<<"-E">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s:~s",[Certificate,Password]))), Dict);
encode_option({proxy,Host,Port}, Dict) ->
	dict:append(<<"-x">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s:~B",[Host,Port]))), Dict);
encode_option({proxy_credentials,UserName,Password}, Dict) ->
	dict:append(<<"-U">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s:~s",[UserName,Password]))), Dict);
encode_option({credentials,UserName,Password}, Dict) ->
	dict:append(<<"-u">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s:~s",[UserName,Password]))), Dict);
encode_option({header,Header}, Dict) ->
	dict:append(<<"-H">>, erlang:list_to_binary((Header), Dict);
encode_option({download,File}, Dict) ->
	dict:append(<<"-o">>, erlang:list_to_binary((File), Dict);
encode_option({data,Name,Value}, Dict) ->
	dict:append(<<"--data">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s=~s",[Name,Value]))), Dict);
encode_option({body_data,Value}, Dict) when is_binary(Value) ->
	dict:append(<<"--data">>, erlang:list_to_binary((binary_to_list(Value)), Dict);
encode_option({body_data,Value}, Dict) ->
	dict:append(<<"--data">>, erlang:list_to_binary(( Value), Dict);
encode_option({file_data,File}, Dict) ->
	dict:append(<<"--data">>, erlang:list_to_binary((lists:flatten(io_lib:format("@~s",[File]))), Dict);
encode_option({form_data,Name,Value}, Dict) ->
	dict:append(<<"--form">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s=~s",[Name,Value]))), Dict);
encode_option({form_file,Name,File}, Dict) ->
	dict:append(<<"--form">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s=@~s",[Name,File]))), Dict);
encode_option({binary_file_data,BinaryFile}, Dict) ->
	dict:append(<<"-T">>, erlang:list_to_binary((lists:flatten(io_lib:format("~s",[BinaryFile]))), Dict);
encode_option({conn_timeout,Seconds}, Dict)->
	dict:append(<<"--connect-timeout">>, erlang:list_to_binary((integer_to_list(Seconds)), Dict);
encode_option({cookie,File}, Dict)->
	dict:append(<<"-c">>, erlang:list_to_binary((File), Dict);
encode_option({set_cookie,File}, Dict)->
	dict:append(<<"-b">>, erlang:list_to_binary((File), Dict);
encode_option({redirect}, Dict) ->
	dict:append(<<"-L">>, <<"-L">>, Dict);
encode_option({include_headers}, Dict)->
	dict:append(<<"-i">>, <<"-i">>, Dict);
encode_option({headers_only}, Dict)->
	dict:append(<<"-I">>, <<"-I">>, Dict);
encode_option({pinnedpubkey, Value}, Dict)->
	dict:append(<<"--pinnedpubkey">>, erlang:list_to_binary((Value), Dict);

encode_option({insecure,_Insecure}, Dict)->
	dict:append(<<"-k">>, <<"-k">>, Dict);

encode_option({options, undefined}, Dict) ->
	Dict.
